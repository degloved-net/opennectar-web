// Two-process interoperability check across platforms and architectures:
//
//   nectar_net_interop_peer host  <port> [wrong]
//   nectar_net_interop_peer guest <address> <port> [wrong]
//
// Both sides derive the same fixed test credentials; "wrong" flips one bit of
// the guest's secret. A correct pair authenticates, exchanges a reliable and
// an unreliable message and exits 0. A wrong guest must be refused (exit 0 on
// the expected refusal). Not a ctest: it needs two processes, possibly on two
// machines or under an emulator.
#include "nectar_net/gated_transport.h"
#include "nectar_net/gns_transport.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>

namespace {

using namespace nectar::net;

JoinCredentials testCredentials(bool wrong)
{
    JoinCredentials value;
    for (std::size_t i = 0; i < 16; ++i) {
        value.sessionNonce[i] = static_cast<std::uint8_t>(0x10U + i);
        value.joinSecret[i] = static_cast<std::uint8_t>(0xa0U + i);
    }
    if (wrong) value.joinSecret[7] ^= 1;
    return value;
}

int fail(const char* what)
{
    std::printf("interop: FAIL %s\n", what);
    return 1;
}

int runHost(std::uint16_t port, bool expectRefusal)
{
    GatedTransport host(std::make_unique<GnsTransport>(), GatedTransport::Role::Host, testCredentials(false));
    if (!host.listen({"0.0.0.0", port})) return fail("listen");
    std::printf("interop: host listening on %u\n", port);
    std::fflush(stdout);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    bool gotReliable = false;
    while (std::chrono::steady_clock::now() < deadline) {
        TransportEvent event;
        while (host.poll(event)) {
            if (event.type == TransportEventType::Connected) {
                if (expectRefusal) return fail("wrong guest was accepted");
                std::printf("interop: host authenticated guest\n");
            } else if (event.type == TransportEventType::Message) {
                if (event.delivery == Delivery::ReliableOrdered && event.payload.size() == 5
                    && std::memcmp(event.payload.data(), "hello", 5) == 0) {
                    gotReliable = true;
                    const std::uint8_t reply[] = {'p', 'i', 'k', 'm', 'i', 'n'};
                    host.send(Delivery::Unreliable, reply, sizeof(reply));
                    host.send(Delivery::ReliableOrdered, reply, sizeof(reply));
                }
            } else if (event.type == TransportEventType::Disconnected && gotReliable) {
                std::printf("interop: host PASS\n");
                return 0;
            }
        }
        if (expectRefusal && host.rejectedPeers() > 0) {
            std::printf("interop: host refused wrong guest (%s) PASS\n", joinErrorString(host.lastJoinError()));
            // Keep the process alive so the flushed refusal reaches the guest.
            for (int i = 0; i < 100; ++i) {
                host.poll(event);
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            return 0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return fail("host timed out");
}

int runGuest(const std::string& address, std::uint16_t port, bool wrong)
{
    GatedTransport guest(std::make_unique<GnsTransport>(), GatedTransport::Role::Guest, testCredentials(wrong));
    if (!guest.connect({address, port})) return fail("connect");

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (std::chrono::steady_clock::now() < deadline) {
        TransportEvent event;
        while (guest.poll(event)) {
            if (event.type == TransportEventType::Connected) {
                if (wrong) return fail("wrong secret was accepted");
                const std::uint8_t hello[] = {'h', 'e', 'l', 'l', 'o'};
                if (!guest.send(Delivery::ReliableOrdered, hello, sizeof(hello))) return fail("send");
            } else if (event.type == TransportEventType::Message && event.delivery == Delivery::ReliableOrdered) {
                std::printf("interop: guest got reply PASS\n");
                guest.disconnectPeer(true);
                // Let the close reach the host before the process exits.
                for (int i = 0; i < 100; ++i) {
                    guest.poll(event);
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
                return 0;
            } else if (event.type == TransportEventType::Error || event.type == TransportEventType::Disconnected) {
                if (wrong && guest.lastJoinError() == JoinError::Refused) {
                    std::printf("interop: guest was refused PASS\n");
                    return 0;
                }
                std::printf("interop: guest error: %s\n", event.detail.c_str());
                return 1;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return fail("guest timed out");
}

} // namespace

int main(int argc, char** argv)
{
    const std::string mode = argc > 1 ? argv[1] : "";
    if (mode == "host" && argc >= 3) {
        const bool wrong = argc > 3 && std::string(argv[3]) == "wrong";
        return runHost(static_cast<std::uint16_t>(std::atoi(argv[2])), wrong);
    }
    if (mode == "guest" && argc >= 4) {
        const bool wrong = argc > 4 && std::string(argv[4]) == "wrong";
        return runGuest(argv[2], static_cast<std::uint16_t>(std::atoi(argv[3])), wrong);
    }
    std::printf("usage: %s host <port> [wrong] | guest <address> <port> [wrong]\n", argv[0]);
    return 2;
}
