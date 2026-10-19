#pragma once

#include "nectar_net/connection_code.h"
#include "nectar_net/net_platform.h"
#include "nectar_net/port_mapping.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace nectar::net {

// UPnP IGD control, implemented with miniupnpc when NECTAR_NET_ENABLE_UPNP is
// on. Blocking; SSDP discovery stays on the LAN.
class IUpnpClient {
public:
    virtual ~IUpnpClient() = default;
    virtual MappingError discover(std::uint32_t timeoutMs) = 0;
    // leaseSeconds may come back 0 when the router only grants permanent
    // mappings; withdraw() must then remove it explicitly.
    virtual MappingError addMapping(std::uint16_t internalPort, std::uint16_t externalPort, std::uint32_t leaseSeconds,
                                    Ipv4Address& externalAddress, std::uint32_t& grantedLeaseSeconds) = 0;
    virtual MappingError deleteMapping(std::uint16_t externalPort) = 0;
};

// Null when the build has no UPnP support.
std::unique_ptr<IUpnpClient> createUpnpClient();

// How a guest on the Internet can reach this host, for the UI to explain.
enum class Reachability {
    Public,             // IPv4 mapping with a public external address
    Ipv6Only,           // no IPv4 mapping, but global IPv6 (firewall may still block)
    BehindCgnat,        // the "external" address is private/shared: CGNAT or double NAT
    NoAutomaticMapping, // nothing worked: manual port forwarding needed
};

const char* reachabilityString(Reachability reachability);

struct PublishResult {
    Reachability reachability = Reachability::NoAutomaticMapping;
    MappingMethod method = MappingMethod::None;
    PortMapping mapping;
    // Endpoints for the connection code, lowest priority value first:
    // global IPv6 (10), mapped public IPv4 (20), LAN IPv4 (30).
    std::vector<DirectEndpoint> endpoints;
    // Why the automatic methods failed, for diagnostics (never contains
    // addresses).
    MappingError pcpError = MappingError::None;
    MappingError natPmpError = MappingError::None;
    MappingError upnpError = MappingError::None;
};

struct PublisherBackends {
    std::function<bool(Ipv4Address&)> findGateway;
    std::function<std::unique_ptr<IDatagramChannel>(const Ipv4Address&)> openChannel;
    std::function<std::unique_ptr<IUpnpClient>()> openUpnp;
    std::function<LocalAddresses()> listAddresses;
    std::function<bool(std::uint8_t*, std::size_t)> randomBytes;
    RetryPolicy retry;
    std::uint32_t upnpDiscoveryMs = 2000;

    // The real operating system, sockets and miniupnpc.
    static PublisherBackends system();
};

// Publishes one UDP port for the lifetime of a room. Blocking: run it on a
// worker thread, never on the game thread.
class PortPublisher {
public:
    explicit PortPublisher(PublisherBackends backends = PublisherBackends::system());
    ~PortPublisher();

    PortPublisher(const PortPublisher&) = delete;
    PortPublisher& operator=(const PortPublisher&) = delete;

    PublishResult publish(std::uint16_t localPort, std::uint32_t lifetimeSeconds = 7200, bool includeLan = true);

    // Renews the mapping once half its lifetime has passed. Returns true when
    // the published endpoints changed (the room must issue a new code).
    bool maintain(std::uint64_t nowMs, PublishResult& current);

    // Removes the mapping from the router. Safe to call more than once.
    void withdraw();

private:
    bool mapIpv4(std::uint16_t localPort, std::uint32_t lifetimeSeconds, PublishResult& result);
    std::vector<DirectEndpoint> endpointsFor(const PublishResult& result, bool includeLan) const;

    PublisherBackends mBackends;
    std::unique_ptr<IDatagramChannel> mChannel;
    std::unique_ptr<IUpnpClient> mUpnp;
    MappingRequest mRequest;
    PortMapping mMapping;
    MappingMethod mMethod = MappingMethod::None;
    std::uint64_t mRenewAtMs = 0;
    bool mIncludeLan = true;
    Ipv4Address mLanAddress{};
    std::vector<Ipv6Address> mIpv6;
};

} // namespace nectar::net
