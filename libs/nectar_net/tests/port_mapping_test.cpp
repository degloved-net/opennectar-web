#include "nectar_net/port_mapping.h"

#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

namespace {

using namespace nectar::net;

int failures = 0;

void check(bool condition, const char* message)
{
    if (!condition) {
        std::printf("FAIL: %s\n", message);
        ++failures;
    }
}

std::uint16_t u16(const std::vector<std::uint8_t>& d, std::size_t at)
{
    return static_cast<std::uint16_t>((d[at] << 8) | d[at + 1]);
}

std::uint32_t u32(const std::vector<std::uint8_t>& d, std::size_t at)
{
    return (static_cast<std::uint32_t>(d[at]) << 24) | (static_cast<std::uint32_t>(d[at + 1]) << 16)
         | (static_cast<std::uint32_t>(d[at + 2]) << 8) | d[at + 3];
}

void put16(std::vector<std::uint8_t>& d, std::size_t at, std::uint16_t v)
{
    d[at] = static_cast<std::uint8_t>(v >> 8);
    d[at + 1] = static_cast<std::uint8_t>(v);
}

void put32(std::vector<std::uint8_t>& d, std::size_t at, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i) d[at + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(v >> (24 - 8 * i));
}

// Simulated home gateway speaking PCP and/or NAT-PMP.
struct FakeGateway final : IDatagramChannel {
    bool pcp = true;
    bool natPmp = true;
    bool silent = false;
    std::uint8_t pcpResultCode = 0;
    std::uint16_t natPmpResultCode = 0;
    int dropFirst = 0;            // requests ignored before answering
    bool forgeBefore = false;     // send junk answers before the real one
    Ipv4Address external{198, 51, 100, 20};
    std::uint16_t assignedPort = 40000;
    std::uint32_t grantedLifetime = 600;
    std::deque<std::vector<std::uint8_t>> pending;
    std::vector<std::vector<std::uint8_t>> requests;

    Ipv4Address localAddress() const override { return {192, 168, 1, 23}; }

    bool send(const std::uint8_t* data, std::size_t size) override
    {
        std::vector<std::uint8_t> req(data, data + size);
        requests.push_back(req);
        if (silent || dropFirst-- > 0) return true;
        if (forgeBefore) {
            pending.push_back({0xde, 0xad});
            std::vector<std::uint8_t> wrongNonce = answerPcp(req);
            if (wrongNonce.size() >= 36) wrongNonce[30] ^= 0xff;
            pending.push_back(wrongNonce);
            std::vector<std::uint8_t> wrongPort = answerPcp(req);
            if (wrongPort.size() >= 42) wrongPort[41] ^= 1;
            pending.push_back(wrongPort);
        }
        if (req.size() >= 1 && req[0] == 2) {
            if (pcp) {
                pending.push_back(answerPcp(req));
            } else if (natPmp) {
                // NAT-PMP-only box: version 0 "unsupported version" reply.
                std::vector<std::uint8_t> r(8, 0);
                r[1] = static_cast<std::uint8_t>(0x80 | req[1]);
                put16(r, 2, 1);
                pending.push_back(r);
            }
        } else if (req.size() >= 2 && req[0] == 0 && natPmp) {
            pending.push_back(answerNatPmp(req));
        }
        return true;
    }

    bool receive(std::vector<std::uint8_t>& output, std::uint32_t) override
    {
        if (pending.empty()) return false;
        output = pending.front();
        pending.pop_front();
        return true;
    }

    std::vector<std::uint8_t> answerPcp(const std::vector<std::uint8_t>& req) const
    {
        std::vector<std::uint8_t> r(60, 0);
        r[0] = 2;
        r[1] = 0x81;
        r[3] = pcpResultCode;
        const std::uint32_t asked = u32(req, 4);
        put32(r, 4, asked == 0 ? 0 : (asked < grantedLifetime ? asked : grantedLifetime));
        put32(r, 8, 1234);
        std::memcpy(r.data() + 24, req.data() + 24, 12); // nonce
        r[36] = req[36];
        put16(r, 40, u16(req, 40));
        put16(r, 42, asked == 0 ? 0 : assignedPort);
        r[54] = 0xff;
        r[55] = 0xff;
        std::memcpy(r.data() + 56, external.data(), 4);
        return r;
    }

    std::vector<std::uint8_t> answerNatPmp(const std::vector<std::uint8_t>& req) const
    {
        if (req[1] == 0) {
            std::vector<std::uint8_t> r(12, 0);
            r[1] = 0x80;
            put16(r, 2, natPmpResultCode);
            put32(r, 4, 99);
            std::memcpy(r.data() + 8, external.data(), 4);
            return r;
        }
        std::vector<std::uint8_t> r(16, 0);
        r[1] = 0x81;
        put16(r, 2, natPmpResultCode);
        put32(r, 4, 99);
        put16(r, 8, u16(req, 4));
        const std::uint32_t asked = u32(req, 8);
        put16(r, 10, asked == 0 ? 0 : assignedPort);
        put32(r, 12, asked == 0 ? 0 : (asked < grantedLifetime ? asked : grantedLifetime));
        return r;
    }
};

MappingRequest request(std::uint32_t lifetime = 3600)
{
    MappingRequest r;
    r.internalPort = 39999;
    r.suggestedExternalPort = 39999;
    r.lifetimeSeconds = lifetime;
    for (std::size_t i = 0; i < r.nonce.size(); ++i) r.nonce[i] = static_cast<std::uint8_t>(0xc0 + i);
    return r;
}

const RetryPolicy kFast{1, 4};

void testClassification()
{
    check(classifyIpv4({192, 168, 1, 1}) == AddressScope::Private, "192.168/16 is private");
    check(classifyIpv4({10, 1, 2, 3}) == AddressScope::Private, "10/8 is private");
    check(classifyIpv4({172, 31, 0, 1}) == AddressScope::Private && classifyIpv4({172, 32, 0, 1}) == AddressScope::Global,
          "172.16/12 boundaries");
    check(classifyIpv4({100, 64, 0, 1}) == AddressScope::SharedCgnat
              && classifyIpv4({100, 127, 255, 255}) == AddressScope::SharedCgnat
              && classifyIpv4({100, 128, 0, 1}) == AddressScope::Global,
          "100.64/10 is carrier-grade NAT");
    check(classifyIpv4({127, 0, 0, 1}) == AddressScope::Loopback, "loopback");
    check(classifyIpv4({169, 254, 3, 4}) == AddressScope::LinkLocal, "link-local");
    check(classifyIpv4({0, 0, 0, 0}) == AddressScope::Unspecified, "unspecified");
    check(classifyIpv4({203, 0, 113, 9}) == AddressScope::Reserved && classifyIpv4({224, 0, 0, 1}) == AddressScope::Reserved,
          "documentation and multicast are reserved");
    check(classifyIpv4({8, 8, 8, 8}) == AddressScope::Global, "public IPv4 is global");

    Ipv6Address v6{};
    check(classifyIpv6(v6) == AddressScope::Unspecified, "::");
    v6[15] = 1;
    check(classifyIpv6(v6) == AddressScope::Loopback, "::1");
    v6 = {0xfe, 0x80};
    check(classifyIpv6(v6) == AddressScope::LinkLocal, "fe80::/10");
    v6 = {0xfd, 0x12};
    check(classifyIpv6(v6) == AddressScope::Private, "ULA fc00::/7");
    v6 = {0x20, 0x01, 0x0d, 0xb8};
    check(classifyIpv6(v6) == AddressScope::Reserved, "2001:db8::/32 is documentation");
    v6 = {0x2a, 0x02, 0x12, 0x34};
    check(classifyIpv6(v6) == AddressScope::Global, "2000::/3 is global");
    v6 = {0xff, 0x02};
    check(classifyIpv6(v6) == AddressScope::Reserved, "multicast is reserved");
    v6 = {};
    v6[10] = 0xff;
    v6[11] = 0xff;
    v6[12] = 100;
    v6[13] = 70;
    check(classifyIpv6(v6) == AddressScope::SharedCgnat, "IPv4-mapped addresses use the IPv4 rules");
}

void testPcpCodec()
{
    const std::vector<std::uint8_t> req = encodePcpMapRequest(request(), {192, 168, 1, 23});
    check(req.size() == kPcpMapRequestBytes && req[0] == 2 && req[1] == 1, "PCP MAP header");
    check(u32(req, 4) == 3600 && req[18] == 0xff && req[19] == 0xff && req[20] == 192 && req[23] == 23,
          "PCP lifetime and IPv4-mapped client address");
    check(req[36] == 17 && u16(req, 40) == 39999 && u16(req, 42) == 39999, "PCP MAP protocol and ports");

    FakeGateway gw;
    PortMapping mapping;
    const std::vector<std::uint8_t> ok = gw.answerPcp(req);
    check(decodePcpMapResponse(ok.data(), ok.size(), request(), mapping) == MappingError::None, "PCP answer decodes");
    check(mapping.externalPort == 40000 && mapping.lifetimeSeconds == 600 && mapping.externalAddress[0] == 198,
          "PCP mapping fields");
    std::vector<std::uint8_t> bad = ok;
    bad[25] ^= 1;
    check(decodePcpMapResponse(bad.data(), bad.size(), request(), mapping) == MappingError::BadResponse,
          "foreign nonce is ignored");
    bad = ok;
    bad.push_back(0);
    check(decodePcpMapResponse(bad.data(), bad.size(), request(), mapping) == MappingError::BadResponse,
          "size not a multiple of 4 is ignored");
    bad = ok;
    bad[1] = 0x82;
    check(decodePcpMapResponse(bad.data(), bad.size(), request(), mapping) == MappingError::BadResponse,
          "other opcode is ignored");
    bad = ok;
    bad[36] = 6;
    check(decodePcpMapResponse(bad.data(), bad.size(), request(), mapping) == MappingError::BadResponse,
          "TCP mapping is ignored");
    bad = ok;
    bad[3] = 2;
    check(decodePcpMapResponse(bad.data(), bad.size(), request(), mapping) == MappingError::NotAuthorized,
          "NOT_AUTHORIZED is reported");
    std::vector<std::uint8_t> headerOnly(ok.begin(), ok.begin() + 24);
    headerOnly[3] = 2;
    check(decodePcpMapResponse(headerOnly.data(), headerOnly.size(), request(), mapping) == MappingError::BadResponse,
          "a bodyless refusal that does not echo the nonce is ignored");
    headerOnly[3] = 1;
    check(decodePcpMapResponse(headerOnly.data(), headerOnly.size(), request(), mapping)
              == MappingError::UnsupportedVersion,
          "bodyless UNSUPP_VERSION is accepted");
    bad = ok;
    bad[54] = 0;
    check(decodePcpMapResponse(bad.data(), bad.size(), request(), mapping) == MappingError::BadResponse,
          "IPv6 external address is refused for an IPv4 mapping");
    std::vector<std::uint8_t> huge(1104, 0);
    check(decodePcpMapResponse(huge.data(), huge.size(), request(), mapping) == MappingError::BadResponse,
          "oversized answers are ignored");
    check(decodePcpMapResponse(nullptr, 60, request(), mapping) == MappingError::BadResponse, "null input");
}

void testNatPmpCodec()
{
    const std::vector<std::uint8_t> req = encodeNatPmpMapRequest(request());
    check(req.size() == kNatPmpMapRequestBytes && req[0] == 0 && req[1] == 1 && u16(req, 4) == 39999
              && u32(req, 8) == 3600,
          "NAT-PMP map request");
    const std::vector<std::uint8_t> del = encodeNatPmpMapRequest(request(0));
    check(u16(del, 6) == 0 && u32(del, 8) == 0, "NAT-PMP delete asks for port 0 and lifetime 0");

    FakeGateway gw;
    PortMapping mapping;
    std::vector<std::uint8_t> ok = gw.answerNatPmp(req);
    check(decodeNatPmpMapResponse(ok.data(), ok.size(), request(), mapping) == MappingError::None
              && mapping.externalPort == 40000 && mapping.lifetimeSeconds == 600,
          "NAT-PMP answer decodes");
    std::vector<std::uint8_t> bad = ok;
    bad[9] ^= 1;
    check(decodeNatPmpMapResponse(bad.data(), bad.size(), request(), mapping) == MappingError::BadResponse,
          "NAT-PMP answer for another port is ignored");
    bad = ok;
    bad.pop_back();
    check(decodeNatPmpMapResponse(bad.data(), bad.size(), request(), mapping) == MappingError::BadResponse,
          "short NAT-PMP answer is ignored");
    Ipv4Address address{};
    std::vector<std::uint8_t> addr = gw.answerNatPmp(encodeNatPmpAddressRequest());
    check(decodeNatPmpAddressResponse(addr.data(), addr.size(), address) == MappingError::None && address[3] == 20,
          "NAT-PMP external address");
}

void testExchanges()
{
    {
        FakeGateway gw;
        PortMapping mapping;
        check(pcpMap(gw, request(), mapping, kFast) == MappingError::None && mapping.method == MappingMethod::Pcp,
              "PCP gateway maps the port");
    }
    {
        FakeGateway gw;
        gw.pcp = false;
        PortMapping mapping;
        check(pcpMap(gw, request(), mapping, kFast) == MappingError::UnsupportedVersion,
              "NAT-PMP-only gateway answers PCP with unsupported version");
        check(natPmpMap(gw, request(), mapping, kFast) == MappingError::None && mapping.method == MappingMethod::NatPmp
                  && mapping.externalAddress[0] == 198 && mapping.externalPort == 40000,
              "NAT-PMP maps and reports the external address");
    }
    {
        FakeGateway gw;
        gw.silent = true;
        PortMapping mapping;
        check(pcpMap(gw, request(), mapping, kFast) == MappingError::Timeout && gw.requests.size() == 4,
              "silent gateway times out after the retry budget");
    }
    {
        FakeGateway gw;
        gw.dropFirst = 2;
        PortMapping mapping;
        check(pcpMap(gw, request(), mapping, kFast) == MappingError::None && gw.requests.size() == 3,
              "lost requests are retransmitted");
    }
    {
        FakeGateway gw;
        gw.forgeBefore = true;
        PortMapping mapping;
        check(pcpMap(gw, request(), mapping, kFast) == MappingError::None && mapping.externalPort == 40000,
              "junk and forged answers do not end the exchange");
    }
    {
        FakeGateway gw;
        gw.pcpResultCode = 2;
        PortMapping mapping;
        check(pcpMap(gw, request(), mapping, kFast) == MappingError::NotAuthorized, "router refusal is reported");
    }
    {
        FakeGateway gw;
        gw.pcp = false;
        gw.natPmpResultCode = 3;
        PortMapping mapping;
        check(natPmpMap(gw, request(), mapping, kFast) == MappingError::NetworkFailure,
              "NAT-PMP network failure is reported");
    }
    {
        FakeGateway gw;
        PortMapping mapping;
        check(pcpMap(gw, request(0), mapping, kFast) == MappingError::None && mapping.lifetimeSeconds == 0,
              "PCP deletes a mapping with lifetime 0");
        gw.pcp = false;
        check(natPmpMap(gw, request(0), mapping, kFast) == MappingError::None && mapping.lifetimeSeconds == 0,
              "NAT-PMP deletes a mapping with lifetime 0");
    }
    check(std::string(mappingErrorString(MappingError::NotAuthorized)) == "not_authorized"
              && std::string(mappingMethodString(MappingMethod::Pcp)) == "PCP",
          "stable strings");
}

} // namespace

int main()
{
    testClassification();
    testPcpCodec();
    testNatPmpCodec();
    testExchanges();
    std::printf("nectar_net_port_mapping_test: %s\n", failures == 0 ? "all tests passed" : "FAILED");
    return failures == 0 ? 0 : 1;
}
