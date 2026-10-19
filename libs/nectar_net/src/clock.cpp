#include "nectar_net/clock.h"

#include "nectar_net/byte_io.h"

#include <algorithm>

namespace nectar::net {

namespace {

constexpr std::uint32_t kMaxRttMs = 10000;
// 1000/30 ms per tick, kept exact by working in thousandths of a tick.
constexpr std::int64_t kMsPerKiloTick = 1000000 / kTicksPerSecond; // 33333 ms per 1000 ticks

} // namespace

std::vector<std::uint8_t> encodePing(const PingPayload& ping)
{
    ByteWriter writer(kPingBytes);
    writer.writeU32(ping.id);
    writer.writeU64(ping.senderTimeMs);
    return writer.ok() ? writer.bytes() : std::vector<std::uint8_t>{};
}

bool decodePing(const std::uint8_t* data, std::size_t size, PingPayload& ping)
{
    if (data == nullptr || size != kPingBytes) return false;
    ByteReader reader(data, size);
    return reader.readU32(ping.id) && reader.readU64(ping.senderTimeMs);
}

std::vector<std::uint8_t> encodePong(const PongPayload& pong)
{
    ByteWriter writer(kPongBytes);
    writer.writeU32(pong.id);
    writer.writeU64(pong.echoTimeMs);
    writer.writeU64(pong.hostTick);
    writer.writeU32(pong.hostTickPhaseMs);
    return writer.ok() ? writer.bytes() : std::vector<std::uint8_t>{};
}

bool decodePong(const std::uint8_t* data, std::size_t size, PongPayload& pong)
{
    if (data == nullptr || size != kPongBytes) return false;
    ByteReader reader(data, size);
    PongPayload decoded;
    if (!reader.readU32(decoded.id) || !reader.readU64(decoded.echoTimeMs) || !reader.readU64(decoded.hostTick)
        || !reader.readU32(decoded.hostTickPhaseMs)) {
        return false;
    }
    if (decoded.hostTickPhaseMs > 1000 / kTicksPerSecond + 1) return false;
    pong = decoded;
    return true;
}

bool ClockEstimator::addSample(const PongPayload& pong, std::uint64_t receivedAtMs)
{
    if (pong.echoTimeMs > receivedAtMs) return false;
    const std::uint64_t rtt64 = receivedAtMs - pong.echoTimeMs;
    if (rtt64 > kMaxRttMs) return false;
    const std::uint32_t rtt = static_cast<std::uint32_t>(rtt64);

    // The pong left the host about rtt/2 before it arrived.
    const std::int64_t hostNowLocal = static_cast<std::int64_t>(receivedAtMs) - static_cast<std::int64_t>(rtt / 2);
    const std::int64_t tickStartLocal = hostNowLocal - static_cast<std::int64_t>(pong.hostTickPhaseMs);
    Sample sample;
    sample.rtt = rtt;
    sample.hostEpochLocalMs =
        tickStartLocal - static_cast<std::int64_t>(pong.hostTick) * kMsPerKiloTick / 1000;
    mWindow[mNext] = sample;
    mNext = (mNext + 1) % kWindow;
    if (mSamples < kWindow) ++mSamples;

    if (mSamples == 1) {
        mSmoothedRtt = rtt;
        mJitter = rtt / 4;
    } else {
        // RFC 6298-style smoothing: srtt += (rtt - srtt)/8, var += (|d| - var)/4.
        const std::int64_t delta = static_cast<std::int64_t>(rtt) - mSmoothedRtt;
        mSmoothedRtt = static_cast<std::uint32_t>(mSmoothedRtt + delta / 8);
        const std::int64_t deviation = delta < 0 ? -delta : delta;
        mJitter = static_cast<std::uint32_t>(mJitter + (deviation - static_cast<std::int64_t>(mJitter)) / 4);
    }
    return true;
}

std::uint32_t ClockEstimator::minRttMs() const
{
    std::uint32_t best = kMaxRttMs;
    for (std::size_t i = 0; i < mSamples; ++i) best = std::min(best, mWindow[i].rtt);
    return mSamples == 0 ? 0 : best;
}

std::uint64_t ClockEstimator::hostTickAt(std::uint64_t nowMs, std::uint32_t* phaseMs) const
{
    if (mSamples == 0) return 0;
    // Trust the sample with the lowest RTT: it suffered the least queueing.
    std::size_t best = 0;
    for (std::size_t i = 1; i < mSamples; ++i)
        if (mWindow[i].rtt < mWindow[best].rtt) best = i;
    const std::int64_t sinceEpoch = static_cast<std::int64_t>(nowMs) - mWindow[best].hostEpochLocalMs;
    if (sinceEpoch <= 0) {
        if (phaseMs != nullptr) *phaseMs = 0;
        return 0;
    }
    const std::int64_t kiloTicks = sinceEpoch * 1000;
    const std::uint64_t tick = static_cast<std::uint64_t>(kiloTicks / kMsPerKiloTick);
    if (phaseMs != nullptr)
        *phaseMs = static_cast<std::uint32_t>((kiloTicks % kMsPerKiloTick) / 1000);
    return tick;
}

std::uint32_t ClockEstimator::interpolationDelayMs() const
{
    return std::clamp<std::uint32_t>(60 + 2 * mJitter, 60, 150);
}

} // namespace nectar::net
