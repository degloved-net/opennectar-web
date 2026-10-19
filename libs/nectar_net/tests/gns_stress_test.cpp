// Lifecycle and load checks for the gated GNS transport over real loopback UDP:
// a reliable burst arrives complete and in order, a room survives many
// connect/leave cycles, and transports can be destroyed while connected.
#include "nectar_net/gated_transport.h"
#include "nectar_net/gns_transport.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <thread>

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

JoinCredentials credentials()
{
    JoinCredentials value;
    for (std::size_t i = 0; i < 16; ++i) {
        value.sessionNonce[i] = static_cast<std::uint8_t>(0x30U + i);
        value.joinSecret[i] = static_cast<std::uint8_t>(0x60U + i);
    }
    return value;
}

std::unique_ptr<GatedTransport> make(GatedTransport::Role role)
{
    return std::make_unique<GatedTransport>(std::make_unique<GnsTransport>(), role, credentials());
}

template <typename Step>
bool pumpUntil(GatedTransport& a, GatedTransport& b, Step step, int seconds = 10)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (std::chrono::steady_clock::now() < deadline) {
        TransportEvent event;
        while (a.poll(event))
            if (step(event, true)) return true;
        while (b.poll(event))
            if (step(event, false)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

bool connectPair(GatedTransport& host, GatedTransport& guest, const Endpoint& endpoint)
{
    if (!guest.connect(endpoint)) return false;
    int connected = 0;
    return pumpUntil(host, guest, [&](const TransportEvent& event, bool) {
        if (event.type == TransportEventType::Connected) ++connected;
        return connected == 2;
    });
}

void testReliableBurst(const Endpoint& endpoint)
{
    auto host = make(GatedTransport::Role::Host);
    auto guest = make(GatedTransport::Role::Guest);
    check(host->listen(endpoint), "burst: host listens");
    check(connectPair(*host, *guest, endpoint), "burst: pair connects");

    constexpr std::uint32_t kCount = 2000;
    std::uint8_t message[1024];
    std::memset(message, 0xab, sizeof(message));
    std::uint32_t sent = 0;
    std::uint32_t expected = 0;
    bool inOrder = true;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (expected < kCount && std::chrono::steady_clock::now() < deadline) {
        // Keep the sender busy without unbounded local queueing.
        TransportStats stats;
        while (sent < kCount && guest->stats(stats) && stats.pendingReliableBytes < 256 * 1024) {
            std::memcpy(message, &sent, sizeof(sent));
            if (!guest->send(Delivery::ReliableOrdered, message, sizeof(message))) break;
            ++sent;
        }
        TransportEvent event;
        while (host->poll(event)) {
            if (event.type != TransportEventType::Message) continue;
            std::uint32_t index = 0;
            if (event.payload.size() == sizeof(message)) std::memcpy(&index, event.payload.data(), sizeof(index));
            inOrder = inOrder && index == expected;
            ++expected;
        }
        while (guest->poll(event)) {
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const bool done = expected == kCount;
    check(done && sent == kCount, "burst: every reliable message arrives");
    check(inOrder, "burst: reliable messages keep their order");
    guest->close();
    host->close();
}

void testConnectLeaveCycles(const Endpoint& endpoint)
{
    auto host = make(GatedTransport::Role::Host);
    check(host->listen(endpoint), "cycles: host listens");
    int completed = 0;
    for (int cycle = 0; cycle < 30; ++cycle) {
        auto guest = make(GatedTransport::Role::Guest);
        if (!connectPair(*host, *guest, endpoint)) break;
        if (cycle % 2 == 0) {
            guest->disconnectPeer(true);
        } else {
            guest.reset(); // abrupt: destroyed while connected
        }
        const bool left = pumpUntil(*host, *host, [&](const TransportEvent& event, bool) {
            return event.type == TransportEventType::Disconnected;
        });
        if (!left || host->state() != TransportState::Listening) break;
        ++completed;
    }
    check(completed == 30, "cycles: the room survives 30 joins and leaves");
    check(host->rejectedPeers() == 0 && host->bannedOrigins() == 0, "cycles: honest guests are never penalised");
    host.reset(); // destroyed while listening
}

void testDestroyHostWhileConnected(const Endpoint& endpoint)
{
    auto host = make(GatedTransport::Role::Host);
    auto guest = make(GatedTransport::Role::Guest);
    check(host->listen(endpoint), "teardown: host listens");
    check(connectPair(*host, *guest, endpoint), "teardown: pair connects");
    host.reset();
    bool sawEnd = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!sawEnd && std::chrono::steady_clock::now() < deadline) {
        TransportEvent event;
        while (guest->poll(event))
            sawEnd = sawEnd || event.type == TransportEventType::Disconnected
                  || event.type == TransportEventType::Error;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    check(sawEnd, "teardown: guest learns that the host is gone");
}

} // namespace

int main()
{
    testReliableBurst({"127.0.0.1", 39881});
    testConnectLeaveCycles({"127.0.0.1", 39882});
    testDestroyHostWhileConnected({"127.0.0.1", 39883});
    std::printf("nectar_net_gns_stress_test: %s\n", failures == 0 ? "all tests passed" : "FAILED");
    return failures == 0 ? 0 : 1;
}
