#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace nectar::net {

// One player's input for one 30 Hz simulation tick, in the game-neutral form
// both games produce before their controllers read it (PADStatus plus the
// port's extra buttons). `flags` carries game-defined bits such as Pikmin 1's
// Bomb/Lock-On/Charge/Swarm (#93); `cursorDx` its cursor rotation.
struct NetInputFrame {
    std::uint64_t tick = 0;
    std::uint16_t held = 0;
    std::uint16_t pressed = 0;
    std::uint16_t released = 0;
    std::int8_t stickX = 0;
    std::int8_t stickY = 0;
    std::int8_t cstickX = 0;
    std::int8_t cstickY = 0;
    std::uint8_t triggerL = 0;
    std::uint8_t triggerR = 0;
    std::int16_t cursorDx = 0;
    std::int16_t cursorDy = 0;
    std::uint8_t flags = 0;

    bool operator==(const NetInputFrame& other) const;
    bool operator!=(const NetInputFrame& other) const { return !(*this == other); }
};

// What a game accepts; anything outside is refused by the decoder.
struct InputLimits {
    std::uint16_t buttonMask = 0xffff;
    std::uint8_t flagMask = 0xff;
    // Stick values arrive as -128..127; -128 is never produced by the games.
    bool allowStickMinimum = false;
};

constexpr std::size_t kInputFrameBytes = 17;   // without the tick
constexpr std::size_t kMaxInputFrames = 6;     // current tick + 5 previous
constexpr std::size_t kInputBundleHeaderBytes = 9;

// Bundle: newest tick (u64) + count (u8) + count frames for newest, newest-1...
// Redundancy repairs isolated losses without retransmission.
std::vector<std::uint8_t> encodeInputBundle(const std::vector<NetInputFrame>& newestFirst);
bool decodeInputBundle(const std::uint8_t* data, std::size_t size, const InputLimits& limits,
                       std::vector<NetInputFrame>& newestFirst);

// AppliedInputs (exact replica): what the host simulated, per tick, as an
// opaque fixed-size blob (Pikmin 1: P1 and P2, 16 bytes each).
//   newest tick u64 | count u8 (1..8) | blob size u8 (1..64) | count blobs, newest first
constexpr std::size_t kMaxAppliedTicks = 8;
constexpr std::size_t kMaxAppliedBlobBytes = 64;
std::vector<std::uint8_t> encodeAppliedInputs(std::uint64_t newestTick,
                                              const std::vector<std::vector<std::uint8_t>>& newestFirst);
bool decodeAppliedInputs(const std::uint8_t* data, std::size_t size, std::size_t expectedBlobBytes,
                         std::uint64_t& newestTick, std::vector<std::vector<std::uint8_t>>& newestFirst);

struct InputPolicy {
    std::uint32_t maxFutureTicks = 30; // ahead of the host: up to 1 s of clock error
    std::uint32_t holdTicks = 6;       // repeat the last state this long across a gap (200 ms)
    std::uint32_t historyTicks = 64;   // frames kept waiting to be consumed
    // Flag bits that are a held state (e.g. Pikmin 1 Swarm) and survive a
    // repeated tick; the others are one-shot presses and never repeat.
    std::uint8_t continuousFlags = 0;
};

enum class InputHealth {
    Live,      // the frame for this tick arrived
    Repeated,  // missing: last state held
    Neutral,   // missing too long: neutral input, show the link problem
};

// Host-side buffer for the remote player's input. Frames for ticks already
// consumed, too far in the future, or duplicated are ignored; consumption
// rebuilds pressed/released from held transitions so a lost frame cannot
// swallow a press or repeat one.
class InputReceiver {
public:
    explicit InputReceiver(InputPolicy policy = {});

    // Returns how many new frames were stored.
    std::size_t receive(const std::vector<NetInputFrame>& newestFirst, std::uint64_t hostTick);

    // Input to simulate `tick` with. Ticks must be consumed in increasing order.
    NetInputFrame consume(std::uint64_t tick, InputHealth& health);

    std::uint64_t ignoredFrames() const { return mIgnored; }
    const InputPolicy& policy() const { return mPolicy; }

private:
    InputPolicy mPolicy;
    std::vector<NetInputFrame> mSlots;
    std::vector<bool> mFilled;
    std::uint64_t mNextTick = 0;
    bool mStarted = false;
    NetInputFrame mLast;
    std::uint64_t mLastLiveTick = 0;
    bool mHaveLast = false;
    std::uint64_t mIgnored = 0;
};

} // namespace nectar::net
