#include "nectar_net/port_publisher.h"

#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
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

// Router state shared by the fake channel and the fake UPnP client.
struct Router {
    bool hasGateway = true;
    bool pcp = false;
    bool natPmp = false;
    bool upnp = false;
    Ipv4Address external{81, 32, 10, 20};
    std::uint16_t externalPort = 40000;
    std::uint32_t grantedLifetime = 600;
    int maps = 0;
    int deletes = 0;
};

struct FakeChannel final : IDatagramChannel {
    Router& router;
    std::deque<std::vector<std::uint8_t>> pending;
    explicit FakeChannel(Router& r) : router(r) {}

    Ipv4Address localAddress() const override { return {192, 168, 1, 23}; }

    static void put16(std::vector<std::uint8_t>& d, std::size_t at, std::uint16_t v)
    {
        d[at] = static_cast<std::uint8_t>(v >> 8);
        d[at + 1] = static_cast<std::uint8_t>(v);
    }
    static void put32(std::vector<std::uint8_t>& d, std::size_t at, std::uint32_t v)
    {
        for (int i = 0; i < 4; ++i) d[at + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(v >> (24 - 8 * i));
    }
    static std::uint32_t get32(const std::uint8_t* p)
    {
        return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16)
             | (static_cast<std::uint32_t>(p[2]) << 8) | p[3];
    }

    bool send(const std::uint8_t* data, std::size_t size) override
    {
        if (size == 60 && data[0] == 2) { // PCP MAP
            if (router.pcp) {
                const std::uint32_t asked = get32(data + 4);
                asked == 0 ? ++router.deletes : ++router.maps;
                std::vector<std::uint8_t> r(60, 0);
                r[0] = 2;
                r[1] = 0x81;
                put32(r, 4, asked == 0 ? 0 : router.grantedLifetime);
                std::memcpy(r.data() + 24, data + 24, 13);
                std::memcpy(r.data() + 40, data + 40, 2);
                put16(r, 42, asked == 0 ? 0 : router.externalPort);
                r[54] = r[55] = 0xff;
                std::memcpy(r.data() + 56, router.external.data(), 4);
                pending.push_back(r);
            } else if (router.natPmp) {
                std::vector<std::uint8_t> r(8, 0);
                r[1] = 0x81;
                put16(r, 2, 1);
                pending.push_back(r);
            }
        } else if (size == 12 && data[0] == 0 && data[1] == 1 && router.natPmp) {
            const std::uint32_t asked = get32(data + 8);
            asked == 0 ? ++router.deletes : ++router.maps;
            std::vector<std::uint8_t> r(16, 0);
            r[1] = 0x81;
            std::memcpy(r.data() + 8, data + 4, 2);
            put16(r, 10, asked == 0 ? 0 : router.externalPort);
            put32(r, 12, asked == 0 ? 0 : router.grantedLifetime);
            pending.push_back(r);
        } else if (size == 2 && data[0] == 0 && data[1] == 0 && router.natPmp) {
            std::vector<std::uint8_t> r(12, 0);
            r[1] = 0x80;
            std::memcpy(r.data() + 8, router.external.data(), 4);
            pending.push_back(r);
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
};

struct FakeUpnp final : IUpnpClient {
    Router& router;
    explicit FakeUpnp(Router& r) : router(r) {}
    MappingError discover(std::uint32_t) override { return router.upnp ? MappingError::None : MappingError::Timeout; }
    MappingError addMapping(std::uint16_t, std::uint16_t, std::uint32_t lease, Ipv4Address& external,
                            std::uint32_t& granted) override
    {
        ++router.maps;
        external = router.external;
        granted = lease;
        return MappingError::None;
    }
    MappingError deleteMapping(std::uint16_t) override
    {
        ++router.deletes;
        return MappingError::None;
    }
};

PublisherBackends backends(Router& router, LocalAddresses addresses = {})
{
    PublisherBackends b;
    b.findGateway = [&router](Ipv4Address& gateway) {
        gateway = {192, 168, 1, 1};
        return router.hasGateway;
    };
    b.openChannel = [&router](const Ipv4Address&) { return std::make_unique<FakeChannel>(router); };
    b.openUpnp = [&router]() { return std::make_unique<FakeUpnp>(router); };
    b.listAddresses = [addresses]() { return addresses; };
    b.randomBytes = [](std::uint8_t* out, std::size_t size) {
        for (std::size_t i = 0; i < size; ++i) out[i] = static_cast<std::uint8_t>(i + 1);
        return true;
    };
    b.retry = RetryPolicy{1, 2};
    return b;
}

LocalAddresses withIpv6()
{
    LocalAddresses local;
    local.ipv4.push_back({192, 168, 1, 23});
    local.ipv6.push_back({0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7}); // link-local: ignored
    local.ipv6.push_back({0x2a, 0x02, 0x12, 0x34, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 9});
    return local;
}

const DirectEndpoint* find(const PublishResult& r, std::uint8_t priority)
{
    for (const DirectEndpoint& e : r.endpoints)
        if (e.priority == priority) return &e;
    return nullptr;
}

void testPcpPublic()
{
    Router router;
    router.pcp = true;
    {
        PortPublisher publisher(backends(router));
        const PublishResult r = publisher.publish(39999);
        check(r.reachability == Reachability::Public && r.method == MappingMethod::Pcp, "PCP router: public host");
        const DirectEndpoint* v4 = find(r, 20);
        check(v4 != nullptr && v4->address[0] == 81 && v4->port == 40000, "code carries the mapped public endpoint");
        const DirectEndpoint* lan = find(r, 30);
        check(lan != nullptr && lan->address[0] == 192 && lan->port == 39999, "code carries the LAN endpoint");
        check(router.maps == 1, "one mapping request");
    }
    check(router.deletes == 1, "destroying the publisher removes the mapping");
}

void testNatPmpFallback()
{
    Router router;
    router.natPmp = true;
    PortPublisher publisher(backends(router));
    const PublishResult r = publisher.publish(39999);
    check(r.method == MappingMethod::NatPmp && r.reachability == Reachability::Public
              && r.pcpError == MappingError::UnsupportedVersion,
          "NAT-PMP-only router is used after PCP's unsupported-version answer");
    publisher.withdraw();
    publisher.withdraw();
    check(router.deletes == 1, "withdraw is idempotent");
}

void testUpnpFallback()
{
    Router router;
    router.upnp = true;
    PortPublisher publisher(backends(router));
    const PublishResult r = publisher.publish(39999);
    check(r.method == MappingMethod::Upnp && r.reachability == Reachability::Public
              && r.pcpError == MappingError::Timeout,
          "silent PCP falls back to UPnP");
    const DirectEndpoint* v4 = find(r, 20);
    check(v4 != nullptr && v4->port == 39999, "UPnP keeps the same external port");
}

void testNoGatewayStillTriesUpnp()
{
    Router router;
    router.hasGateway = false;
    router.upnp = true;
    PortPublisher publisher(backends(router));
    const PublishResult r = publisher.publish(39999);
    check(r.method == MappingMethod::Upnp && r.pcpError == MappingError::NoGateway,
          "an unknown gateway (Android) still allows UPnP");
}

void testCgnat()
{
    Router router;
    router.pcp = true;
    router.external = {100, 72, 5, 9};
    PortPublisher publisher(backends(router));
    const PublishResult r = publisher.publish(39999);
    check(r.reachability == Reachability::BehindCgnat && r.method == MappingMethod::Pcp,
          "a shared 100.64/10 external address means CGNAT");
    check(find(r, 20) == nullptr, "no unreachable public endpoint is advertised");
    check(router.deletes == 1, "the useless mapping is removed at once");
    router.external = {192, 168, 0, 2};
    const PublishResult nested = publisher.publish(39999);
    check(nested.reachability == Reachability::BehindCgnat, "a private external address means double NAT");
}

void testNothingWorks()
{
    Router router;
    PortPublisher publisher(backends(router));
    PublishResult r = publisher.publish(39999);
    check(r.reachability == Reachability::NoAutomaticMapping && r.method == MappingMethod::None,
          "no PCP, NAT-PMP or UPnP: manual forwarding");
    check(r.endpoints.size() == 1 && r.endpoints[0].priority == 30, "only the LAN endpoint remains");
    PortPublisher withV6(backends(router, withIpv6()));
    r = withV6.publish(39999);
    check(r.reachability == Reachability::Ipv6Only, "global IPv6 still offers a way in");
    const DirectEndpoint* v6 = find(r, 10);
    check(v6 != nullptr && v6->family == DirectEndpoint::Family::Ipv6 && v6->address[0] == 0x2a && v6->port == 39999
              && r.endpoints.size() == 2,
          "only the global IPv6 address is advertised, never link-local");
    PortPublisher noLan(backends(router));
    check(noLan.publish(39999, 7200, false).endpoints.empty(), "the LAN endpoint is optional");
}

void testRenewal()
{
    Router router;
    router.pcp = true;
    PortPublisher publisher(backends(router));
    PublishResult current = publisher.publish(39999, 7200);
    check(!publisher.maintain(1000, current) && router.maps == 1, "first maintain only schedules the renewal");
    check(!publisher.maintain(1000 + 299000, current) && router.maps == 1, "no renewal before half the lifetime");
    check(!publisher.maintain(1000 + 300000, current) && router.maps == 2, "renewed at half the lifetime");
    router.externalPort = 40001;
    check(publisher.maintain(1000 + 600000, current) && router.maps == 3, "a changed endpoint is reported");
    const DirectEndpoint* v4 = find(current, 20);
    check(v4 != nullptr && v4->port == 40001, "the new endpoint replaces the old one");
    router.pcp = false;
    check(!publisher.maintain(1000 + 900000, current), "a failed renewal keeps the old endpoint");
    router.pcp = true;
    check(!publisher.maintain(1000 + 900000 + 29999, current) && router.maps == 3, "retry waits 30 s");
    check(!publisher.maintain(1000 + 900000 + 30000, current) && router.maps == 4, "retry after 30 s");
}

} // namespace

int main()
{
    testPcpPublic();
    testNatPmpFallback();
    testUpnpFallback();
    testNoGatewayStillTriesUpnp();
    testCgnat();
    testNothingWorks();
    testRenewal();
    check(std::string(reachabilityString(Reachability::BehindCgnat)) == "behind_cgnat", "stable strings");
    std::printf("nectar_net_port_publisher_test: %s\n", failures == 0 ? "all tests passed" : "FAILED");
    return failures == 0 ? 0 : 1;
}
