#include "nectar_net/replication.h"

#include "nectar_net/byte_io.h"

#include <cmath>
#include <cstring>

namespace nectar::net {

namespace {

std::uint32_t floatBits(float value)
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

float bitsFloat(std::uint32_t bits)
{
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

bool finiteWithin(float value, float limit) { return std::isfinite(value) && std::fabs(value) <= limit; }

float length(const Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }

Vec3 sub(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 add(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 scale(const Vec3& v, float s) { return {v.x * s, v.y * s, v.z * s}; }

float lerpAngle(float a, float b, float t)
{
    constexpr float kTwoPi = 6.28318530718f;
    float d = std::fmod(b - a, kTwoPi);
    if (d > kTwoPi / 2) d -= kTwoPi;
    if (d < -kTwoPi / 2) d += kTwoPi;
    return a + d * t;
}

} // namespace

std::vector<std::uint8_t> encodeCaptainState(const CaptainState& s)
{
    ByteWriter writer(kCaptainStateBytes);
    for (float f : {s.position.x, s.position.y, s.position.z, s.velocity.x, s.velocity.y, s.velocity.z,
                    s.faceDirection}) {
        writer.writeU32(floatBits(f));
    }
    writer.writeU8(s.player);
    return writer.ok() ? writer.bytes() : std::vector<std::uint8_t>{};
}

bool decodeCaptainState(const std::uint8_t* data, std::size_t size, CaptainState& state)
{
    if (data == nullptr || size != kCaptainStateBytes) return false;
    ByteReader reader(data, size);
    float values[7];
    for (float& value : values) {
        std::uint32_t bits = 0;
        if (!reader.readU32(bits)) return false;
        value = bitsFloat(bits);
    }
    CaptainState decoded;
    if (!reader.readU8(decoded.player) || decoded.player > 1) return false;
    for (int i = 0; i < 3; ++i)
        if (!finiteWithin(values[i], kMaxWorldCoordinate) || !finiteWithin(values[3 + i], kMaxSpeed)) return false;
    if (!finiteWithin(values[6], 1000.0f)) return false;
    decoded.position = {values[0], values[1], values[2]};
    decoded.velocity = {values[3], values[4], values[5]};
    decoded.faceDirection = values[6];
    state = decoded;
    return true;
}

EntityRecord captainRecord(std::uint16_t sceneEpoch, const CaptainState& state)
{
    NetEntityId id;
    id.sceneEpoch = sceneEpoch;
    id.kind = EntityKind::Captain;
    id.counter = static_cast<std::uint32_t>(state.player) + 1;
    EntityRecord record;
    record.id = id.pack();
    record.schema = kCaptainSchema;
    record.data = encodeCaptainState(state);
    return record;
}

std::uint32_t inputLeadTicks(std::uint32_t rttMs, std::uint32_t jitterMs)
{
    // One way plus two jitters, rounded up to whole ticks. A late input is
    // not lost (the host repeats the held state for a tick), and the guest
    // draws its captain ahead of the replica, so no extra safety tick.
    const std::uint32_t budget = rttMs / 2 + 2 * jitterMs;
    const std::uint32_t ticks = (budget * 30 + 999) / 1000;
    return ticks < 1 ? 1 : ticks > 8 ? 8 : ticks;
}

PredictionCorrector::PredictionCorrector(Settings settings) : mSettings(settings) {}

void PredictionCorrector::recordPredicted(std::uint64_t tick, const Vec3& simulated)
{
    Entry& entry = mHistory[tick % kHistory];
    entry.tick = tick;
    entry.expected = add(simulated, mOutstanding);
    entry.valid = true;
}

float PredictionCorrector::reconcile(std::uint64_t tick, const Vec3& authoritative)
{
    const Entry& entry = mHistory[tick % kHistory];
    if (!entry.valid || entry.tick != tick) return 0.0f;
    const Vec3 error = sub(authoritative, entry.expected);
    const float distance = length(error);
    if (distance < mSettings.ignoreDistance) return distance;
    // Every later prediction was made from the same wrong position.
    for (Entry& later : mHistory)
        if (later.valid && later.tick >= tick) later.expected = add(later.expected, error);
    mOutstanding = add(mOutstanding, error);
    ++mCorrections;
    if (distance >= mSettings.snapDistance) {
        mSnapPending = true;
        ++mSnaps;
    } else {
        mTicksLeft = mSettings.smoothTicks == 0 ? 1 : mSettings.smoothTicks;
        mPerTick = scale(mOutstanding, 1.0f / static_cast<float>(mTicksLeft));
    }
    return distance;
}

Vec3 PredictionCorrector::step(bool& snapped)
{
    snapped = false;
    if (mSnapPending) {
        mSnapPending = false;
        mTicksLeft = 0;
        snapped = true;
        const Vec3 all = mOutstanding;
        mOutstanding = Vec3{};
        return all;
    }
    if (mTicksLeft == 0) return Vec3{};
    --mTicksLeft;
    const Vec3 amount = mTicksLeft == 0 ? mOutstanding : mPerTick;
    mOutstanding = sub(mOutstanding, amount);
    return amount;
}

void SnapshotInterpolator::push(std::uint64_t tick, const CaptainState& state)
{
    if (mCount != 0 && tick <= mEntries[mCount - 1].tick) return;
    if (mCount == kCapacity) {
        for (std::size_t i = 1; i < kCapacity; ++i) mEntries[i - 1] = mEntries[i];
        --mCount;
    }
    mEntries[mCount++] = Entry{tick, state};
}

bool SnapshotInterpolator::sample(std::uint64_t tickTimes1000, CaptainState& out) const
{
    if (mCount == 0) return false;
    const Entry& first = mEntries[0];
    const Entry& last = mEntries[mCount - 1];
    if (tickTimes1000 <= first.tick * 1000) {
        out = first.state;
        return true;
    }
    if (tickTimes1000 >= last.tick * 1000) {
        out = last.state;
        const std::uint64_t ahead = tickTimes1000 - last.tick * 1000;
        const std::uint64_t capped =
            ahead < static_cast<std::uint64_t>(mMaxExtrapolation) * 1000 ? ahead : mMaxExtrapolation * 1000ULL;
        // Velocity is per second (scaled by the frame time in the games).
        out.position = add(last.state.position, scale(last.state.velocity, static_cast<float>(capped) / 30000.0f));
        return true;
    }
    for (std::size_t i = 1; i < mCount; ++i) {
        const Entry& b = mEntries[i];
        if (tickTimes1000 > b.tick * 1000) continue;
        const Entry& a = mEntries[i - 1];
        const float t = static_cast<float>(tickTimes1000 - a.tick * 1000) / static_cast<float>((b.tick - a.tick) * 1000);
        out = b.state;
        out.position = add(a.state.position, scale(sub(b.state.position, a.state.position), t));
        out.velocity = add(a.state.velocity, scale(sub(b.state.velocity, a.state.velocity), t));
        out.faceDirection = lerpAngle(a.state.faceDirection, b.state.faceDirection, t);
        return true;
    }
    out = last.state;
    return true;
}

} // namespace nectar::net
