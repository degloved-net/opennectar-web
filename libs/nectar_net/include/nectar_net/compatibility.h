#pragma once

#include "nectar_net/join_gate.h"
#include "nectar_net/transport.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace nectar::net {

// Second step after the join quarantine: both peers prove they run the same
// game, region, simulation build and content before anything is loaded. The
// guest offers its CompatibilityInfo; the host compares and answers with the
// first mismatching field plus its own info, so the UI can explain the
// refusal ("the host plays the PAL version") without guessing.

enum class GameId : std::uint8_t {
    Pikmin1 = 1,
    Pikmin2 = 2,
};

struct CompatibilityInfo {
    std::uint16_t protocolMinor = 0;
    std::uint8_t game = 0;
    // Game-defined region code (e.g. retail version). Must match exactly.
    std::uint8_t region = 0;
    // Simulation/build manifest digest (source tree, ABI, game version).
    std::array<std::uint8_t, 32> build{};
    // Gameplay content digest: data files and logic mods that both must share.
    std::array<std::uint8_t, 32> content{};
    // Features this build supports, and those it needs from the other peer.
    std::uint32_t capabilities = 0;
    std::uint32_t requiredCapabilities = 0;
};

enum class CompatibilityField : std::uint8_t {
    None = 0,
    Game = 1,
    Region = 2,
    Build = 3,
    Content = 4,
    Capabilities = 5,
};

constexpr std::size_t kCompatibilityInfoBytes = 76;
constexpr std::uint64_t kCompatibilityTimeoutMs = 10000;

bool encodeCompatibilityInfo(const CompatibilityInfo& info, std::vector<std::uint8_t>& output);
bool decodeCompatibilityInfo(const std::uint8_t* data, std::size_t size, CompatibilityInfo& info);

// First mismatching field in a fixed order (game, region, build, content,
// capabilities), or None when the pair may play together.
CompatibilityField compareCompatibility(const CompatibilityInfo& host, const CompatibilityInfo& guest);

const char* compatibilityFieldString(CompatibilityField field);

enum class CompatibilityState {
    Idle,
    AwaitingOffer,  // host: waiting for the guest's info
    AwaitingResult, // guest: info sent, waiting for the verdict
    Compatible,
    Incompatible,
    Failed,         // protocol violation or timeout
};

// Transport-independent state machine, driven like the join gates.
class CompatibilityCheck {
public:
    enum class Role { Host, Guest };

    CompatibilityCheck(Role role, const CompatibilityInfo& local, std::uint64_t timeoutMs = kCompatibilityTimeoutMs);

    // Called once the join quarantine has authenticated the peer. The guest
    // writes its offer into output and returns Send.
    JoinAction start(std::uint64_t nowMs, std::vector<std::uint8_t>& output);
    JoinAction onMessage(Delivery delivery, const std::uint8_t* data, std::size_t size,
                         std::vector<std::uint8_t>& output);
    JoinAction tick(std::uint64_t nowMs);
    void reset();

    CompatibilityState state() const { return mState; }
    CompatibilityField mismatch() const { return mMismatch; }
    // Valid once the other side's info arrived (Compatible or Incompatible).
    const CompatibilityInfo& peer() const { return mPeer; }

private:
    JoinAction fail();

    Role mRole;
    CompatibilityInfo mLocal;
    CompatibilityInfo mPeer;
    std::uint64_t mTimeoutMs;
    std::uint64_t mDeadlineMs = 0;
    CompatibilityState mState = CompatibilityState::Idle;
    CompatibilityField mMismatch = CompatibilityField::None;
};

} // namespace nectar::net
