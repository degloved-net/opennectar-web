#pragma once

#include "nectar_net/world.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace nectar::net {

// Captain replication shared by both games: the host's authoritative state,
// the guest's prediction/correction of its own captain and the interpolation
// of the other one. Engine-free; the game adapters read and write the Navi.

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

// Schema 1 of EntityKind::Captain records (29 bytes, big-endian IEEE-754).
struct CaptainState {
    Vec3 position;
    Vec3 velocity;
    float faceDirection = 0.0f; // radians
    std::uint8_t player = 0;    // 0 host captain, 1 guest captain
};

constexpr std::uint8_t kCaptainSchema = 1;
constexpr std::size_t kCaptainStateBytes = 29;
// Anything beyond this is not a position in either game's maps.
constexpr float kMaxWorldCoordinate = 100000.0f;
constexpr float kMaxSpeed = 10000.0f;

std::vector<std::uint8_t> encodeCaptainState(const CaptainState& state);
// Refuses NaN/Inf, out-of-range coordinates or speeds and unknown players.
bool decodeCaptainState(const std::uint8_t* data, std::size_t size, CaptainState& state);

EntityRecord captainRecord(std::uint16_t sceneEpoch, const CaptainState& state);

// How many ticks the guest must run ahead of the host so its input arrives
// before the host simulates that tick: one-way delay plus two jitters,
// rounded up, within [1, 8].
std::uint32_t inputLeadTicks(std::uint32_t rttMs, std::uint32_t jitterMs);

// Guest-side correction of the locally predicted captain. History entries
// store where the captain will be once every pending correction is applied,
// so an error is never corrected twice.
class PredictionCorrector {
public:
    static constexpr std::size_t kHistory = 64;

    struct Settings {
        float snapDistance = 60.0f;  // larger errors are applied at once
        float ignoreDistance = 0.5f; // smaller ones are float noise
        std::uint32_t smoothTicks = 4; // ~130 ms, within the plan's 100-150 ms
    };

    explicit PredictionCorrector(Settings settings);
    PredictionCorrector() : PredictionCorrector(Settings{}) {}

    // After simulating `tick`, with the position the simulation produced.
    void recordPredicted(std::uint64_t tick, const Vec3& simulated);

    // Authoritative position for `tick`. Returns the error found (0 when the
    // tick is unknown or too old).
    float reconcile(std::uint64_t tick, const Vec3& authoritative);

    // Offset to add to the simulated position this tick; true when it is a
    // snap (the caller may also reset velocity).
    Vec3 step(bool& snapped);

    std::uint64_t snaps() const { return mSnaps; }
    std::uint64_t corrections() const { return mCorrections; }

private:
    struct Entry {
        std::uint64_t tick = 0;
        Vec3 expected;
        bool valid = false;
    };
    Settings mSettings;
    std::array<Entry, kHistory> mHistory{};
    Vec3 mOutstanding;
    Vec3 mPerTick;
    std::uint32_t mTicksLeft = 0;
    bool mSnapPending = false;
    std::uint64_t mSnaps = 0;
    std::uint64_t mCorrections = 0;
};

// Remote captain shown slightly in the past: linear interpolation between
// the two host states around the requested tick, extrapolation for at most
// maxExtrapolationTicks, then the last state is held.
class SnapshotInterpolator {
public:
    static constexpr std::size_t kCapacity = 32;

    explicit SnapshotInterpolator(std::uint32_t maxExtrapolationTicks = 3)
        : mMaxExtrapolation(maxExtrapolationTicks)
    {
    }

    // Older or repeated ticks are ignored.
    void push(std::uint64_t tick, const CaptainState& state);
    // tickTimes1000: the wanted tick in thousandths (tick*1000 + fraction).
    bool sample(std::uint64_t tickTimes1000, CaptainState& out) const;
    bool empty() const { return mCount == 0; }

private:
    struct Entry {
        std::uint64_t tick = 0;
        CaptainState state;
    };
    std::array<Entry, kCapacity> mEntries{};
    std::size_t mCount = 0; // entries are kept sorted, oldest first
    std::uint32_t mMaxExtrapolation;
};

} // namespace nectar::net
