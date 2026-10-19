#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace nectar::net {

// Port publication for the host without any Internet service: the gateway is
// asked to forward one UDP port (PCP, RFC 6887; NAT-PMP, RFC 6886; UPnP IGD
// elsewhere). Everything here is blocking with short, bounded retries, so it
// must run off the game thread. Responses are parsed strictly: exact sizes,
// matching opcode, nonce, protocol and internal port.

using Ipv4Address = std::array<std::uint8_t, 4>;
using Ipv6Address = std::array<std::uint8_t, 16>;

enum class AddressScope {
    Unspecified,  // 0.0.0.0 / ::
    Loopback,
    LinkLocal,
    Private,      // RFC 1918, IPv6 ULA fc00::/7
    SharedCgnat,  // 100.64.0.0/10: carrier-grade NAT, not reachable from outside
    Reserved,     // documentation, benchmarking, multicast, class E...
    Global,
};

AddressScope classifyIpv4(const Ipv4Address& address);
AddressScope classifyIpv6(const Ipv6Address& address);

enum class MappingMethod : std::uint8_t {
    None,
    Ipv6Global,
    Pcp,
    NatPmp,
    Upnp,
    Manual,
};

enum class MappingError {
    None,
    NoGateway,
    Timeout,         // no answer after every retry: gateway without PCP/NAT-PMP
    UnsupportedVersion,
    NotAuthorized,   // the gateway refuses mappings (disabled by the owner)
    NetworkFailure,  // the gateway has no external address yet
    OutOfResources,
    Refused,         // any other explicit refusal
    BadResponse,     // malformed or mismatching answer, ignored until timeout
    SocketFailure,
};

const char* mappingErrorString(MappingError error);
const char* mappingMethodString(MappingMethod method);

// Datagram path to the gateway's PCP/NAT-PMP port (5351). Real sockets live
// in the platform layer; tests use a simulated gateway.
class IDatagramChannel {
public:
    virtual ~IDatagramChannel() = default;
    virtual bool send(const std::uint8_t* data, std::size_t size) = 0;
    // Waits up to timeoutMs for one datagram from the gateway.
    virtual bool receive(std::vector<std::uint8_t>& output, std::uint32_t timeoutMs) = 0;
    // IPv4 address of the local interface that reaches the gateway (PCP).
    virtual Ipv4Address localAddress() const = 0;
};

struct RetryPolicy {
    // Initial wait, doubled per attempt: 250+500+1000+2000 ms by default. The
    // RFCs allow longer schedules; a player waiting on a menu does not.
    std::uint32_t initialTimeoutMs = 250;
    std::uint32_t attempts = 4;
};

struct MappingRequest {
    std::uint16_t internalPort = 0;
    std::uint16_t suggestedExternalPort = 0; // 0: let the gateway choose
    std::uint32_t lifetimeSeconds = 0;       // 0 deletes the mapping
    std::array<std::uint8_t, 12> nonce{};     // PCP: identifies our mapping
};

struct PortMapping {
    MappingMethod method = MappingMethod::None;
    Ipv4Address externalAddress{};
    std::uint16_t internalPort = 0;
    std::uint16_t externalPort = 0;
    std::uint32_t lifetimeSeconds = 0;
    std::uint32_t epoch = 0;
    std::array<std::uint8_t, 12> nonce{};
};

constexpr std::uint16_t kGatewayMappingPort = 5351;
constexpr std::size_t kPcpMapRequestBytes = 60;
constexpr std::size_t kPcpMaxMessageBytes = 1100;
constexpr std::size_t kNatPmpMapRequestBytes = 12;
constexpr std::size_t kNatPmpMapResponseBytes = 16;
constexpr std::size_t kNatPmpAddressResponseBytes = 12;

// Wire codecs, exposed for tests and fuzzing.
std::vector<std::uint8_t> encodePcpMapRequest(const MappingRequest& request, const Ipv4Address& client);
// Returns None, a gateway result as MappingError, or BadResponse when the
// datagram is not the answer to this request. UnsupportedVersion is also
// returned for a NAT-PMP-only gateway answering in version 0.
MappingError decodePcpMapResponse(const std::uint8_t* data, std::size_t size, const MappingRequest& request,
                                  PortMapping& mapping);
std::vector<std::uint8_t> encodeNatPmpMapRequest(const MappingRequest& request);
MappingError decodeNatPmpMapResponse(const std::uint8_t* data, std::size_t size, const MappingRequest& request,
                                     PortMapping& mapping);
std::vector<std::uint8_t> encodeNatPmpAddressRequest();
MappingError decodeNatPmpAddressResponse(const std::uint8_t* data, std::size_t size, Ipv4Address& address);

// Request/response exchanges with retries. A lifetime of 0 removes the
// mapping (the result then carries lifetime 0).
MappingError pcpMap(IDatagramChannel& channel, const MappingRequest& request, PortMapping& mapping,
                    const RetryPolicy& retry = {});
MappingError natPmpMap(IDatagramChannel& channel, const MappingRequest& request, PortMapping& mapping,
                       const RetryPolicy& retry = {});

} // namespace nectar::net
