#pragma once

#include "nectar_net/compatibility.h"
#include "nectar_net/connection_code.h"
#include "nectar_net/gated_transport.h"
#include "nectar_net/port_publisher.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace nectar::net {

// One-code flow for the launcher of either game. The host opens a room and
// shows one code; the guest pastes it and the answer travels back over the
// connection. No servers are involved. The UI only maps the enums below to
// text; nothing here prints addresses or secrets.

using TransportFactory = std::function<std::unique_ptr<ITransport>()>;

// GnsTransport when built with it, otherwise null.
TransportFactory defaultTransportFactory();

struct RoomConfig {
    std::uint16_t protocolMajor = 1;
    CompatibilityInfo compatibility;
    // 0 picks a random high port (49152-65535); a fixed port helps users who
    // forward it by hand.
    std::uint16_t localPort = 0;
    std::uint32_t codeLifetimeSeconds = 2U * 60U * 60U;
    bool includeLanEndpoint = true;
    // Skip PCP/NAT-PMP/UPnP, for a manually forwarded port.
    bool automaticPortMapping = true;
    // Manual forwarding: the public address the user typed (optional).
    std::vector<DirectEndpoint> manualEndpoints;
};

enum class RoomError {
    None,
    NoTransport,
    ListenFailed,
    RandomSourceFailure,
    NoEndpoint, // nothing to put in the code: no LAN, IPv6, mapping or manual address
};

const char* roomErrorString(RoomError error);

struct RoomStatus {
    std::string code;
    Reachability reachability = Reachability::NoAutomaticMapping;
    MappingMethod method = MappingMethod::None;
    std::uint16_t localPort = 0;
};

class HostRoom {
public:
    HostRoom(TransportFactory factory = defaultTransportFactory(),
             PublisherBackends backends = PublisherBackends::system());
    ~HostRoom();

    HostRoom(const HostRoom&) = delete;
    HostRoom& operator=(const HostRoom&) = delete;

    // Blocking (port mapping can take a few seconds): call from a worker.
    RoomError open(const RoomConfig& config, std::uint64_t nowUnixSeconds, RoomStatus& status);

    // Renews the mapping. True when the code changed and must be shown again;
    // a guest already connected is not affected.
    bool maintain(std::uint64_t nowMs, std::uint64_t nowUnixSeconds, RoomStatus& status);

    // The gated transport: Connected means authenticated and compatible.
    GatedTransport* transport() { return mTransport.get(); }

    // Removes the mapping and forgets the secret; the code stops working.
    void close();

private:
    std::string issueCode(std::uint64_t nowUnixSeconds);

    TransportFactory mFactory;
    std::unique_ptr<PortPublisher> mPublisher;
    std::unique_ptr<GatedTransport> mTransport;
    RoomConfig mConfig;
    ConnectionCode mCode;
    PublishResult mPublished;
};

enum class JoinFailure {
    None,
    BadCode,          // mistyped or truncated: ask the host to copy it again
    Expired,          // too old: ask for a new code
    WrongVersion,     // the host runs another protocol version
    NoTransport,
    Unreachable,      // no endpoint answered: host closed, CGNAT, firewall
    Refused,          // the room rejected the secret (stale room or typo)
    Incompatible,     // see GuestJoin::incompatibility()
    RoomFull,         // someone else already joined
    Lost,             // connected, then the link dropped
};

const char* joinFailureString(JoinFailure failure);

enum class JoinProgress {
    Idle,
    Connecting,
    Connected,
    Failed,
};

// Tries the code's endpoints in priority order, each with its own deadline.
class GuestJoin {
public:
    static constexpr std::uint64_t kAttemptTimeoutMs = 6000;

    explicit GuestJoin(TransportFactory factory = defaultTransportFactory());

    JoinFailure start(const std::string& code, const CompatibilityInfo& local, std::uint16_t protocolMajor,
                      std::uint64_t nowUnixSeconds, std::uint64_t nowMs);

    // Pumps the attempt; forwards session events once connected.
    JoinProgress poll(std::uint64_t nowMs, TransportEvent& event, bool& hasEvent);

    JoinFailure failure() const { return mFailure; }
    CompatibilityField incompatibility() const;
    const CompatibilityInfo* host() const;
    GatedTransport* transport() { return mTransport.get(); }
    std::size_t attemptedEndpoints() const { return mNext; }

    void cancel();

private:
    bool tryNext(std::uint64_t nowMs);

    TransportFactory mFactory;
    std::unique_ptr<GatedTransport> mTransport;
    ConnectionCode mCode;
    CompatibilityInfo mLocal;
    std::vector<DirectEndpoint> mOrder;
    std::size_t mNext = 0;
    std::uint64_t mDeadlineMs = 0;
    JoinProgress mProgress = JoinProgress::Idle;
    JoinFailure mFailure = JoinFailure::None;
};

// Endpoint as text for a transport ("1.2.3.4" or "2001:db8::1").
std::string endpointAddressText(const DirectEndpoint& endpoint);

} // namespace nectar::net
