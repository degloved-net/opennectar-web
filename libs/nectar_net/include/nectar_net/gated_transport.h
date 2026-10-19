#pragma once

#include "nectar_net/compatibility.h"
#include "nectar_net/join_gate.h"
#include "nectar_net/transport.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace nectar::net {

// Wraps any ITransport with the join quarantine of ADR-0002. Callers see
// Connected only after the join secret has been validated; until then no
// message is surfaced and send() refuses gameplay. A host drops a guest that
// fails, times out or speaks out of turn, and keeps listening for another.
//
// Room-blocking defence: an origin (peer IP) that fails the quarantine
// kFailuresBeforeBan times is dropped on sight for a ban that doubles from
// kFirstBanMs up to kMaxBanMs. The table is bounded and only lives in memory;
// addresses are never logged.
class GatedTransport final : public ITransport {
public:
    enum class Role { Host, Guest };

    static constexpr std::uint32_t kFailuresBeforeBan = 3;
    static constexpr std::uint64_t kFirstBanMs        = 30U * 1000U;
    static constexpr std::uint64_t kMaxBanMs          = 10U * 60U * 1000U;
    static constexpr std::size_t kMaxTrackedOrigins   = 64;
    using Clock = std::function<std::uint64_t()>; // monotonic milliseconds

    GatedTransport(std::unique_ptr<ITransport> inner, Role role, const JoinCredentials& credentials,
                   Clock clock = {}, std::uint64_t timeoutMs = kJoinTimeoutMs);
    ~GatedTransport() override;

    GatedTransport(const GatedTransport&) = delete;
    GatedTransport& operator=(const GatedTransport&) = delete;

    bool listen(const Endpoint& endpoint) override;
    bool connect(const Endpoint& endpoint) override;
    bool send(Delivery delivery, const std::uint8_t* data, std::size_t size) override;
    bool poll(TransportEvent& event) override;
    void disconnectPeer(bool flushReliable) override;
    void close() override;
    TransportState state() const override;
    bool stats(TransportStats& output) const override;

    // Adds the compatibility step (game, region, build, content) after the
    // join secret; Connected is then reported only for a compatible pair.
    // Call before listen()/connect().
    void requireCompatibility(const CompatibilityInfo& local);
    // The check, or nullptr when not required; its peer() and mismatch()
    // explain an incompatible pair to the user until the next connect/listen.
    const CompatibilityCheck* compatibility() const { return mCompat.get(); }

    JoinState joinState() const;
    // Last quarantine failure, for diagnostics; never contains secrets.
    JoinError lastJoinError() const { return mLastError; }
    // Guests the host dropped before authentication (scan/flood diagnostics).
    std::uint32_t rejectedPeers() const { return mRejectedPeers; }
    // Origins currently banned (diagnostics; no addresses are exposed).
    std::size_t bannedOrigins() const;

private:
    void handle(TransportEvent& event);
    void apply(JoinAction action, const std::vector<std::uint8_t>& output);
    void dropPeer(JoinAction action, JoinError error);
    void surface(TransportEventType type, const char* detail);
    bool sessionReady() const;
    void onAuthenticated(std::uint64_t now);
    void handleCompatibility(TransportEvent& event, std::vector<std::uint8_t>& output);

    struct Origin {
        std::string address;
        std::uint32_t failures = 0;
        std::uint64_t bannedUntilMs = 0;
        std::uint64_t lastSeenMs = 0;
    };
    Origin& origin(const std::string& address, std::uint64_t now);
    void noteFailure(std::uint64_t now);

    std::unique_ptr<ITransport> mInner;
    Role mRole;
    Clock mClock;
    HostJoinGate mHost;
    GuestJoinGate mGuest;
    std::deque<TransportEvent> mReady;
    JoinError mLastError = JoinError::None;
    std::uint32_t mRejectedPeers = 0;
    std::unique_ptr<CompatibilityCheck> mCompat;
    std::vector<Origin> mOrigins;
    std::string mPeerAddress; // origin of the connection in quarantine
};

} // namespace nectar::net
