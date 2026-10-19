#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace nectar::net {

// Loading barrier for a day, level or cave floor. The host announces a new
// scene epoch (ScenePrepare), the guest loads and answers SceneReady, and the
// host starts both at the same tick (SceneStart). Epochs only grow: anything
// about an older scene is ignored, so a late message cannot rewind the guest.

struct ScenePrepare {
    std::uint32_t epoch = 0;
    std::uint32_t sceneId = 0; // game-defined: level, course or cave floor
    std::uint64_t seed = 0;    // host-chosen, e.g. cave layout
};

struct SceneReady {
    std::uint32_t epoch = 0;
    std::uint32_t sceneId = 0;
};

struct SceneStart {
    std::uint32_t epoch = 0;
    std::uint64_t startTick = 0;
};

std::vector<std::uint8_t> encodeScenePrepare(const ScenePrepare& message);
bool decodeScenePrepare(const std::uint8_t* data, std::size_t size, ScenePrepare& message);
std::vector<std::uint8_t> encodeSceneReady(const SceneReady& message);
bool decodeSceneReady(const std::uint8_t* data, std::size_t size, SceneReady& message);
std::vector<std::uint8_t> encodeSceneStart(const SceneStart& message);
bool decodeSceneStart(const std::uint8_t* data, std::size_t size, SceneStart& message);

enum class BarrierState {
    Idle,
    Preparing,  // host: waiting for the guest; guest: loading
    Ready,      // host: guest loaded; guest: loaded, waiting for start
    Running,
    TimedOut,   // host: the guest never finished loading
};

class HostSceneBarrier {
public:
    explicit HostSceneBarrier(std::uint64_t loadTimeoutMs = 60000) : mTimeoutMs(loadTimeoutMs) {}

    // New scene: returns the ScenePrepare to send reliably.
    ScenePrepare prepare(std::uint32_t sceneId, std::uint64_t seed, std::uint64_t nowMs);
    // False when the message is not about the scene being prepared.
    bool onReady(const SceneReady& ready);
    // Once ready (or when continuing alone after a timeout).
    SceneStart start(std::uint64_t startTick);
    BarrierState tick(std::uint64_t nowMs);

    BarrierState state() const { return mState; }
    std::uint32_t epoch() const { return mEpoch; }

private:
    std::uint64_t mTimeoutMs;
    std::uint64_t mDeadlineMs = 0;
    std::uint32_t mEpoch = 0;
    std::uint32_t mSceneId = 0;
    BarrierState mState = BarrierState::Idle;
};

class GuestSceneBarrier {
public:
    // False for stale or repeated epochs (ignored).
    bool onPrepare(const ScenePrepare& prepare);
    // The game finished loading: returns the SceneReady to send.
    bool loaded(SceneReady& ready);
    // False unless it starts the scene being loaded.
    bool onStart(const SceneStart& start);

    BarrierState state() const { return mState; }
    std::uint32_t epoch() const { return mEpoch; }
    const ScenePrepare& current() const { return mPrepare; }
    std::uint64_t startTick() const { return mStartTick; }

private:
    ScenePrepare mPrepare;
    std::uint32_t mEpoch = 0;
    std::uint64_t mStartTick = 0;
    BarrierState mState = BarrierState::Idle;
};

} // namespace nectar::net
