#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace nectar::net {

// 30 Hz canonical tick shared by both games, independent of render rate.
constexpr std::uint32_t kTicksPerSecond = 30;

struct PingPayload {
    std::uint32_t id = 0;
    std::uint64_t senderTimeMs = 0; // sender's monotonic clock, echoed back
};

struct PongPayload {
    std::uint32_t id = 0;
    std::uint64_t echoTimeMs = 0;
    std::uint64_t hostTick = 0;
    std::uint32_t hostTickPhaseMs = 0; // time since hostTick started, < 34 ms
};

constexpr std::size_t kPingBytes = 12;
constexpr std::size_t kPongBytes = 24;

std::vector<std::uint8_t> encodePing(const PingPayload& ping);
bool decodePing(const std::uint8_t* data, std::size_t size, PingPayload& ping);
std::vector<std::uint8_t> encodePong(const PongPayload& pong);
bool decodePong(const std::uint8_t* data, std::size_t size, PongPayload& pong);

// Guest-side view of the host clock from ping/pong samples. Uses the
// lowest-RTT recent samples for the offset (queueing only ever adds delay)
// and smoothed RTT/jitter for the interpolation buffer. Never uses wall time.
class ClockEstimator {
public:
    static constexpr std::size_t kWindow = 16;

    // Returns false for impossible samples (echo from the future, absurd
    // RTT), which are ignored.
    bool addSample(const PongPayload& pong, std::uint64_t receivedAtMs);

    bool ready() const { return mSamples != 0; }
    std::uint32_t rttMs() const { return mSmoothedRtt; }
    std::uint32_t minRttMs() const;
    std::uint32_t jitterMs() const { return mJitter; }

    // Host tick (with fraction in ms) estimated at the local time `nowMs`.
    std::uint64_t hostTickAt(std::uint64_t nowMs, std::uint32_t* phaseMs = nullptr) const;

    // clamp(60 ms + 2 * jitter, 60 ms, 150 ms): how far behind the newest
    // host state remote entities are shown.
    std::uint32_t interpolationDelayMs() const;

private:
    struct Sample {
        std::uint32_t rtt = 0;
        // Local time at which host tick 0 would have started (ms), possibly
        // negative, stored as signed.
        std::int64_t hostEpochLocalMs = 0;
    };
    Sample mWindow[kWindow];
    std::size_t mNext = 0;
    std::size_t mSamples = 0;
    std::uint32_t mSmoothedRtt = 0;
    std::uint32_t mJitter = 0;
};

} // namespace nectar::net
