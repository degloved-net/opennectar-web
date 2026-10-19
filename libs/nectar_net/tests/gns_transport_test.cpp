#include "nectar_net/gns_transport.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

namespace {

int failures = 0;

void check(bool condition, const char* message)
{
    if (!condition) {
        std::printf("FAIL: %s\n", message);
        ++failures;
    }
}

void dumpPending(nectar::net::GnsTransport& transport)
{
    nectar::net::TransportEvent event;
    while (transport.poll(event)) {
        if (!event.detail.empty()) std::printf("transport detail: %s\n", event.detail.c_str());
    }
}

bool waitFor(nectar::net::GnsTransport& first, nectar::net::GnsTransport& second,
             nectar::net::TransportEventType wanted, nectar::net::TransportEvent& found)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        nectar::net::TransportEvent event;
        while (first.poll(event)) {
            if (event.type == wanted) {
                found = std::move(event);
                return true;
            }
        }
        while (second.poll(event)) {
            if (event.type == wanted) {
                found = std::move(event);
                return true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

} // namespace

int main()
{
    using namespace nectar::net;
    GnsTransport host;
    GnsTransport guest;
    Endpoint endpoint{"127.0.0.1", 39871};
    const bool listening = host.listen(endpoint);
    check(listening, "host listens on direct UDP");
    if (!listening) dumpPending(host);
    const bool connecting = guest.connect(endpoint);
    check(connecting, "guest starts direct connection");
    if (!connecting) dumpPending(guest);

    TransportEvent event;
    check(waitFor(host, guest, TransportEventType::Connected, event), "encrypted connection completes");
    // Both sides receive their own Connected notification.
    check(waitFor(host, guest, TransportEventType::Connected, event), "both peers report connected");

    const std::uint8_t reliable[] = {'r', 'e', 'l'};
    check(guest.send(Delivery::ReliableOrdered, reliable, sizeof(reliable)), "guest sends reliable message");
    check(waitFor(host, guest, TransportEventType::Message, event), "host receives reliable message");
    check(event.delivery == Delivery::ReliableOrdered && event.payload.size() == sizeof(reliable)
              && std::memcmp(event.payload.data(), reliable, sizeof(reliable)) == 0,
          "reliable payload and delivery survive transport");

    const std::uint8_t unreliable[] = {'u', 'n', 'r'};
    check(host.send(Delivery::Unreliable, unreliable, sizeof(unreliable)), "host sends unreliable message");
    check(waitFor(host, guest, TransportEventType::Message, event), "guest receives unreliable message");
    check(event.delivery == Delivery::Unreliable && event.payload.size() == sizeof(unreliable)
              && std::memcmp(event.payload.data(), unreliable, sizeof(unreliable)) == 0,
          "unreliable payload and delivery survive transport");

    host.close();
    guest.close();
    check(host.state() == TransportState::Closed && guest.state() == TransportState::Closed,
          "both transports close cleanly");

    std::printf("nectar_net_gns_transport_test: %s\n", failures == 0 ? "all tests passed" : "FAILED");
    return failures == 0 ? 0 : 1;
}
