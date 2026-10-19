#include "nectar_net/port_mapping.h"

#include <chrono>
#include <cstring>

namespace nectar::net {

namespace {

constexpr std::uint8_t kPcpVersion = 2;
constexpr std::uint8_t kPcpOpMap = 1;
constexpr std::uint8_t kPcpResponseBit = 0x80;
constexpr std::size_t kPcpHeaderBytes = 24;
constexpr std::uint8_t kUdpProtocol = 17;
constexpr std::uint8_t kNatPmpOpAddress = 0;
constexpr std::uint8_t kNatPmpOpMapUdp = 1;
constexpr std::uint8_t kNatPmpResponseBit = 0x80;

void putU16(std::vector<std::uint8_t>& out, std::size_t at, std::uint16_t value)
{
    out[at] = static_cast<std::uint8_t>(value >> 8);
    out[at + 1] = static_cast<std::uint8_t>(value);
}

void putU32(std::vector<std::uint8_t>& out, std::size_t at, std::uint32_t value)
{
    for (int i = 0; i < 4; ++i) out[at + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(value >> (24 - 8 * i));
}

std::uint16_t getU16(const std::uint8_t* p) { return static_cast<std::uint16_t>((p[0] << 8) | p[1]); }

std::uint32_t getU32(const std::uint8_t* p)
{
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16)
         | (static_cast<std::uint32_t>(p[2]) << 8) | p[3];
}

// IPv4-mapped IPv6, as PCP carries every address in 16 bytes.
void putMappedIpv4(std::vector<std::uint8_t>& out, std::size_t at, const Ipv4Address& address)
{
    std::memset(out.data() + at, 0, 10);
    out[at + 10] = 0xff;
    out[at + 11] = 0xff;
    std::memcpy(out.data() + at + 12, address.data(), 4);
}

bool isMappedIpv4(const std::uint8_t* p)
{
    for (int i = 0; i < 10; ++i)
        if (p[i] != 0) return false;
    return p[10] == 0xff && p[11] == 0xff;
}

MappingError pcpResult(std::uint8_t code)
{
    switch (code) {
    case 0: return MappingError::None;
    case 1: return MappingError::UnsupportedVersion;
    case 2: return MappingError::NotAuthorized;
    case 7: return MappingError::NetworkFailure;
    case 8:
    case 10: return MappingError::OutOfResources; // NO_RESOURCES, USER_EX_QUOTA
    default: return MappingError::Refused;
    }
}

MappingError natPmpResult(std::uint16_t code)
{
    switch (code) {
    case 0: return MappingError::None;
    case 1: return MappingError::UnsupportedVersion;
    case 2: return MappingError::NotAuthorized;
    case 3: return MappingError::NetworkFailure;
    case 4: return MappingError::OutOfResources;
    default: return MappingError::Refused;
    }
}

template <typename Decode>
MappingError exchange(IDatagramChannel& channel, const std::vector<std::uint8_t>& request, const RetryPolicy& retry,
                      Decode decode)
{
    using Clock = std::chrono::steady_clock;
    std::uint32_t timeout = retry.initialTimeoutMs;
    std::vector<std::uint8_t> datagram;
    for (std::uint32_t attempt = 0; attempt < retry.attempts; ++attempt) {
        if (!channel.send(request.data(), request.size())) return MappingError::SocketFailure;
        const auto deadline = Clock::now() + std::chrono::milliseconds(timeout);
        for (;;) {
            const auto now = Clock::now();
            if (now >= deadline) break;
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
            if (!channel.receive(datagram, static_cast<std::uint32_t>(left > 0 ? left : 1))) break;
            if (datagram.size() > kPcpMaxMessageBytes) continue;
            const MappingError result = decode(datagram.data(), datagram.size());
            // Anything that is not the answer to this request is ignored, so a
            // stray or forged datagram cannot end the exchange early.
            if (result != MappingError::BadResponse) return result;
        }
        timeout = timeout > UINT32_MAX / 2 ? UINT32_MAX : timeout * 2;
    }
    return MappingError::Timeout;
}

} // namespace

AddressScope classifyIpv4(const Ipv4Address& a)
{
    if (a[0] == 0) return AddressScope::Unspecified;
    if (a[0] == 127) return AddressScope::Loopback;
    if (a[0] == 169 && a[1] == 254) return AddressScope::LinkLocal;
    if (a[0] == 10 || (a[0] == 172 && (a[1] & 0xf0) == 16) || (a[0] == 192 && a[1] == 168))
        return AddressScope::Private;
    if (a[0] == 100 && (a[1] & 0xc0) == 64) return AddressScope::SharedCgnat;
    if ((a[0] == 192 && a[1] == 0 && (a[2] == 0 || a[2] == 2)) || (a[0] == 198 && (a[1] & 0xfe) == 18)
        || (a[0] == 198 && a[1] == 51 && a[2] == 100) || (a[0] == 203 && a[1] == 0 && a[2] == 113) || a[0] >= 224) {
        return AddressScope::Reserved;
    }
    return AddressScope::Global;
}

AddressScope classifyIpv6(const Ipv6Address& a)
{
    bool zeroPrefix = true;
    for (int i = 0; i < 15; ++i) zeroPrefix = zeroPrefix && a[static_cast<std::size_t>(i)] == 0;
    if (zeroPrefix && a[15] == 0) return AddressScope::Unspecified;
    if (zeroPrefix && a[15] == 1) return AddressScope::Loopback;
    if (isMappedIpv4(a.data())) return classifyIpv4({a[12], a[13], a[14], a[15]});
    if (a[0] == 0xfe && (a[1] & 0xc0) == 0x80) return AddressScope::LinkLocal;
    if ((a[0] & 0xfe) == 0xfc) return AddressScope::Private;
    if (a[0] == 0x20 && a[1] == 0x01 && a[2] == 0x0d && a[3] == 0xb8) return AddressScope::Reserved;
    if ((a[0] & 0xe0) == 0x20) return AddressScope::Global;
    return AddressScope::Reserved;
}

const char* mappingErrorString(MappingError error)
{
    switch (error) {
    case MappingError::None:               return "none";
    case MappingError::NoGateway:          return "no_gateway";
    case MappingError::Timeout:            return "timeout";
    case MappingError::UnsupportedVersion: return "unsupported_version";
    case MappingError::NotAuthorized:      return "not_authorized";
    case MappingError::NetworkFailure:     return "network_failure";
    case MappingError::OutOfResources:     return "out_of_resources";
    case MappingError::Refused:            return "refused";
    case MappingError::BadResponse:        return "bad_response";
    case MappingError::SocketFailure:      return "socket_failure";
    }
    return "invalid_mapping_error";
}

const char* mappingMethodString(MappingMethod method)
{
    switch (method) {
    case MappingMethod::None:       return "none";
    case MappingMethod::Ipv6Global: return "IPv6";
    case MappingMethod::Pcp:        return "PCP";
    case MappingMethod::NatPmp:     return "NAT-PMP";
    case MappingMethod::Upnp:       return "UPnP";
    case MappingMethod::Manual:     return "Manual";
    }
    return "invalid";
}

std::vector<std::uint8_t> encodePcpMapRequest(const MappingRequest& request, const Ipv4Address& client)
{
    std::vector<std::uint8_t> out(kPcpMapRequestBytes, 0);
    out[0] = kPcpVersion;
    out[1] = kPcpOpMap;
    putU32(out, 4, request.lifetimeSeconds);
    putMappedIpv4(out, 8, client);
    std::memcpy(out.data() + 24, request.nonce.data(), request.nonce.size());
    out[36] = kUdpProtocol;
    putU16(out, 40, request.internalPort);
    putU16(out, 42, request.suggestedExternalPort);
    putMappedIpv4(out, 44, Ipv4Address{}); // no preferred external address
    return out;
}

MappingError decodePcpMapResponse(const std::uint8_t* data, std::size_t size, const MappingRequest& request,
                                  PortMapping& mapping)
{
    if (data == nullptr || size < 4) return MappingError::BadResponse;
    // A NAT-PMP-only gateway answers an unknown version with its own
    // version-0 "unsupported version" reply (RFC 6887 section 9).
    if (data[0] == 0 && data[1] == (kNatPmpResponseBit | kPcpOpMap) && getU16(data + 2) == 1)
        return MappingError::UnsupportedVersion;
    if (size < kPcpHeaderBytes || size > kPcpMaxMessageBytes || size % 4 != 0) return MappingError::BadResponse;
    if (data[0] != kPcpVersion) {
        return data[3] == 1 ? MappingError::UnsupportedVersion : MappingError::BadResponse;
    }
    if (data[1] != (kPcpResponseBit | kPcpOpMap)) return MappingError::BadResponse;
    const std::uint8_t result = data[3];
    const bool hasMap = size >= kPcpMapRequestBytes;
    if (hasMap) {
        if (std::memcmp(data + 24, request.nonce.data(), request.nonce.size()) != 0 || data[36] != kUdpProtocol
            || getU16(data + 40) != request.internalPort) {
            return MappingError::BadResponse;
        }
    }
    if (result != 0) {
        // Errors may come without the MAP body only when the request itself
        // could not be parsed; anything else must echo our nonce.
        return hasMap || result == 1 || result == 3 ? pcpResult(result) : MappingError::BadResponse;
    }
    if (!hasMap || !isMappedIpv4(data + 44)) return MappingError::BadResponse;
    PortMapping decoded;
    decoded.method = MappingMethod::Pcp;
    decoded.lifetimeSeconds = getU32(data + 4);
    decoded.epoch = getU32(data + 8);
    decoded.internalPort = request.internalPort;
    decoded.externalPort = getU16(data + 42);
    std::memcpy(decoded.externalAddress.data(), data + 56, 4);
    decoded.nonce = request.nonce;
    if (request.lifetimeSeconds != 0 && (decoded.lifetimeSeconds == 0 || decoded.externalPort == 0))
        return MappingError::BadResponse;
    mapping = decoded;
    return MappingError::None;
}

std::vector<std::uint8_t> encodeNatPmpMapRequest(const MappingRequest& request)
{
    std::vector<std::uint8_t> out(kNatPmpMapRequestBytes, 0);
    out[1] = kNatPmpOpMapUdp;
    putU16(out, 4, request.internalPort);
    putU16(out, 6, request.lifetimeSeconds == 0 ? 0 : request.suggestedExternalPort);
    putU32(out, 8, request.lifetimeSeconds);
    return out;
}

MappingError decodeNatPmpMapResponse(const std::uint8_t* data, std::size_t size, const MappingRequest& request,
                                     PortMapping& mapping)
{
    if (data == nullptr || size < 4 || data[0] != 0 || data[1] != (kNatPmpResponseBit | kNatPmpOpMapUdp))
        return MappingError::BadResponse;
    const std::uint16_t result = getU16(data + 2);
    if (result != 0) return size == kNatPmpMapResponseBytes || size == 8 ? natPmpResult(result) : MappingError::BadResponse;
    if (size != kNatPmpMapResponseBytes || getU16(data + 8) != request.internalPort) return MappingError::BadResponse;
    PortMapping decoded;
    decoded.method = MappingMethod::NatPmp;
    decoded.epoch = getU32(data + 4);
    decoded.internalPort = request.internalPort;
    decoded.externalPort = getU16(data + 10);
    decoded.lifetimeSeconds = getU32(data + 12);
    if (request.lifetimeSeconds != 0 && (decoded.lifetimeSeconds == 0 || decoded.externalPort == 0))
        return MappingError::BadResponse;
    mapping = decoded;
    return MappingError::None;
}

std::vector<std::uint8_t> encodeNatPmpAddressRequest()
{
    return std::vector<std::uint8_t>{0, kNatPmpOpAddress};
}

MappingError decodeNatPmpAddressResponse(const std::uint8_t* data, std::size_t size, Ipv4Address& address)
{
    if (data == nullptr || size < 4 || data[0] != 0 || data[1] != (kNatPmpResponseBit | kNatPmpOpAddress))
        return MappingError::BadResponse;
    const std::uint16_t result = getU16(data + 2);
    if (result != 0) return natPmpResult(result);
    if (size != kNatPmpAddressResponseBytes) return MappingError::BadResponse;
    std::memcpy(address.data(), data + 8, 4);
    return MappingError::None;
}

MappingError pcpMap(IDatagramChannel& channel, const MappingRequest& request, PortMapping& mapping,
                    const RetryPolicy& retry)
{
    const std::vector<std::uint8_t> datagram = encodePcpMapRequest(request, channel.localAddress());
    return exchange(channel, datagram, retry, [&](const std::uint8_t* data, std::size_t size) {
        return decodePcpMapResponse(data, size, request, mapping);
    });
}

MappingError natPmpMap(IDatagramChannel& channel, const MappingRequest& request, PortMapping& mapping,
                       const RetryPolicy& retry)
{
    PortMapping result;
    const MappingError mapped = exchange(channel, encodeNatPmpMapRequest(request), retry,
                                         [&](const std::uint8_t* data, std::size_t size) {
                                             return decodeNatPmpMapResponse(data, size, request, result);
                                         });
    if (mapped != MappingError::None) return mapped;
    if (request.lifetimeSeconds != 0) {
        // NAT-PMP reports the external address through a separate request.
        const MappingError address = exchange(channel, encodeNatPmpAddressRequest(), retry,
                                              [&](const std::uint8_t* data, std::size_t size) {
                                                  return decodeNatPmpAddressResponse(data, size,
                                                                                     result.externalAddress);
                                              });
        if (address != MappingError::None) return address;
    }
    mapping = result;
    return MappingError::None;
}

} // namespace nectar::net
