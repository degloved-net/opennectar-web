#include "nectar_net/input.h"

#include "nectar_net/byte_io.h"

namespace nectar::net {

bool NetInputFrame::operator==(const NetInputFrame& o) const
{
    return tick == o.tick && held == o.held && pressed == o.pressed && released == o.released && stickX == o.stickX
        && stickY == o.stickY && cstickX == o.cstickX && cstickY == o.cstickY && triggerL == o.triggerL
        && triggerR == o.triggerR && cursorDx == o.cursorDx && cursorDy == o.cursorDy && flags == o.flags;
}

std::vector<std::uint8_t> encodeInputBundle(const std::vector<NetInputFrame>& newestFirst)
{
    if (newestFirst.empty() || newestFirst.size() > kMaxInputFrames) return {};
    for (std::size_t i = 1; i < newestFirst.size(); ++i)
        if (newestFirst[i].tick + i != newestFirst[0].tick) return {};
    ByteWriter writer(kInputBundleHeaderBytes + newestFirst.size() * kInputFrameBytes);
    writer.writeU64(newestFirst[0].tick);
    writer.writeU8(static_cast<std::uint8_t>(newestFirst.size()));
    for (const NetInputFrame& f : newestFirst) {
        writer.writeU16(f.held);
        writer.writeU16(f.pressed);
        writer.writeU16(f.released);
        writer.writeU8(static_cast<std::uint8_t>(f.stickX));
        writer.writeU8(static_cast<std::uint8_t>(f.stickY));
        writer.writeU8(static_cast<std::uint8_t>(f.cstickX));
        writer.writeU8(static_cast<std::uint8_t>(f.cstickY));
        writer.writeU8(f.triggerL);
        writer.writeU8(f.triggerR);
        writer.writeU16(static_cast<std::uint16_t>(f.cursorDx));
        writer.writeU16(static_cast<std::uint16_t>(f.cursorDy));
        writer.writeU8(f.flags);
    }
    return writer.ok() ? writer.bytes() : std::vector<std::uint8_t>{};
}

bool decodeInputBundle(const std::uint8_t* data, std::size_t size, const InputLimits& limits,
                       std::vector<NetInputFrame>& newestFirst)
{
    if (data == nullptr || size < kInputBundleHeaderBytes) return false;
    ByteReader reader(data, size);
    std::uint64_t tick = 0;
    std::uint8_t count = 0;
    if (!reader.readU64(tick) || !reader.readU8(count)) return false;
    if (count == 0 || count > kMaxInputFrames || tick + 1 < count) return false;
    if (reader.remaining() != static_cast<std::size_t>(count) * kInputFrameBytes) return false;
    std::vector<NetInputFrame> frames(count);
    for (std::uint8_t i = 0; i < count; ++i) {
        NetInputFrame& f = frames[i];
        std::uint8_t sx = 0, sy = 0, cx = 0, cy = 0;
        std::uint16_t dx = 0, dy = 0;
        if (!reader.readU16(f.held) || !reader.readU16(f.pressed) || !reader.readU16(f.released)
            || !reader.readU8(sx) || !reader.readU8(sy) || !reader.readU8(cx) || !reader.readU8(cy)
            || !reader.readU8(f.triggerL) || !reader.readU8(f.triggerR) || !reader.readU16(dx)
            || !reader.readU16(dy) || !reader.readU8(f.flags)) {
            return false;
        }
        f.tick = tick - i;
        f.stickX = static_cast<std::int8_t>(sx);
        f.stickY = static_cast<std::int8_t>(sy);
        f.cstickX = static_cast<std::int8_t>(cx);
        f.cstickY = static_cast<std::int8_t>(cy);
        f.cursorDx = static_cast<std::int16_t>(dx);
        f.cursorDy = static_cast<std::int16_t>(dy);
        const std::uint16_t buttons = f.held | f.pressed | f.released;
        if ((buttons & ~limits.buttonMask) != 0 || (f.flags & ~limits.flagMask) != 0) return false;
        if (!limits.allowStickMinimum
            && (f.stickX == -128 || f.stickY == -128 || f.cstickX == -128 || f.cstickY == -128)) {
            return false;
        }
    }
    newestFirst = std::move(frames);
    return true;
}

std::vector<std::uint8_t> encodeAppliedInputs(std::uint64_t newestTick,
                                              const std::vector<std::vector<std::uint8_t>>& newestFirst)
{
    if (newestFirst.empty() || newestFirst.size() > kMaxAppliedTicks || newestTick + 1 < newestFirst.size()) return {};
    const std::size_t blob = newestFirst[0].size();
    if (blob == 0 || blob > kMaxAppliedBlobBytes) return {};
    for (const auto& b : newestFirst)
        if (b.size() != blob) return {};
    ByteWriter writer(10 + newestFirst.size() * blob);
    writer.writeU64(newestTick);
    writer.writeU8(static_cast<std::uint8_t>(newestFirst.size()));
    writer.writeU8(static_cast<std::uint8_t>(blob));
    for (const auto& b : newestFirst) writer.writeBytes(b.data(), b.size());
    return writer.ok() ? writer.bytes() : std::vector<std::uint8_t>{};
}

bool decodeAppliedInputs(const std::uint8_t* data, std::size_t size, std::size_t expectedBlobBytes,
                         std::uint64_t& newestTick, std::vector<std::vector<std::uint8_t>>& newestFirst)
{
    if (data == nullptr || size < 10) return false;
    ByteReader reader(data, size);
    std::uint64_t tick = 0;
    std::uint8_t count = 0, blob = 0;
    if (!reader.readU64(tick) || !reader.readU8(count) || !reader.readU8(blob)) return false;
    if (count == 0 || count > kMaxAppliedTicks || blob != expectedBlobBytes || blob == 0
        || blob > kMaxAppliedBlobBytes || tick + 1 < count || reader.remaining() != static_cast<std::size_t>(count) * blob)
        return false;
    std::vector<std::vector<std::uint8_t>> blobs(count, std::vector<std::uint8_t>(blob));
    for (auto& b : blobs)
        if (!reader.readBytes(b.data(), blob)) return false;
    newestTick = tick;
    newestFirst = std::move(blobs);
    return true;
}

InputReceiver::InputReceiver(InputPolicy policy)
    : mPolicy(policy), mSlots(policy.historyTicks), mFilled(policy.historyTicks, false)
{
}

std::size_t InputReceiver::receive(const std::vector<NetInputFrame>& newestFirst, std::uint64_t hostTick)
{
    if (mSlots.empty()) return 0;
    if (!mStarted) {
        mNextTick = hostTick;
        mStarted = true;
    }
    std::size_t stored = 0;
    for (const NetInputFrame& frame : newestFirst) {
        const bool past = frame.tick < mNextTick;
        const bool future = frame.tick > hostTick + mPolicy.maxFutureTicks
            || frame.tick >= mNextTick + mSlots.size();
        if (past || future) {
            ++mIgnored;
            continue;
        }
        const std::size_t slot = static_cast<std::size_t>(frame.tick % mSlots.size());
        if (mFilled[slot] && mSlots[slot].tick == frame.tick) continue; // redundant copy
        mSlots[slot] = frame;
        mFilled[slot] = true;
        ++stored;
    }
    return stored;
}

NetInputFrame InputReceiver::consume(std::uint64_t tick, InputHealth& health)
{
    if (!mStarted) {
        mNextTick = tick;
        mStarted = true;
    }
    // Skipped ticks drop whatever they had.
    while (mNextTick < tick) {
        const std::size_t slot = static_cast<std::size_t>(mNextTick % mSlots.size());
        if (mFilled[slot] && mSlots[slot].tick == mNextTick) mFilled[slot] = false;
        ++mNextTick;
    }
    const std::size_t slot = static_cast<std::size_t>(tick % mSlots.size());
    NetInputFrame result;
    result.tick = tick;
    const std::uint16_t previousHeld = mHaveLast ? mLast.held : 0;
    if (!mSlots.empty() && mFilled[slot] && mSlots[slot].tick == tick) {
        result = mSlots[slot];
        mFilled[slot] = false;
        // A held edge is a press/release even if the frame carrying the
        // explicit bit was lost; explicit bits keep sub-tick taps.
        result.pressed = static_cast<std::uint16_t>(result.pressed | (result.held & ~previousHeld));
        result.released = static_cast<std::uint16_t>(result.released | (previousHeld & ~result.held));
        mLastLiveTick = tick;
        health = InputHealth::Live;
    } else if (mHaveLast && tick - mLastLiveTick <= mPolicy.holdTicks) {
        // Continuous state continues; one-shot events are never repeated.
        result = mLast;
        result.tick = tick;
        result.pressed = 0;
        result.released = 0;
        result.flags = static_cast<std::uint8_t>(mLast.flags & mPolicy.continuousFlags);
        result.cursorDx = 0;
        result.cursorDy = 0;
        health = InputHealth::Repeated;
    } else {
        result.released = previousHeld; // let go of everything once
        health = InputHealth::Neutral;
    }
    mLast = result;
    mHaveLast = true;
    mNextTick = tick + 1;
    return result;
}

} // namespace nectar::net
