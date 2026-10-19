#include "nectar_net/gated_transport.h"
#include "nectar_net/gns_transport.h"

#include <chrono>
#include <cstdio>
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

JoinCredentials credentials(std::uint8_t seed)
{
    JoinCredentials value;
    for (std::size_t i = 0; i < 16; ++i) {
        value.sessionNonce[i] = static_cast<std::uint8_t>(seed + i);
        value.joinSecret[i] = static_cast<std::uint8_t>(seed + 0x80U + i);
    }
    return value;
}

std::unique_ptr<GatedTransport> make(GatedTransport::Role role, const JoinCredentials& creds)
{
    return std::make_unique<GatedTransport>(std::make_unique<GnsTransport>(), role, creds);
}

// Polls every transport until `done` holds or five seconds pass. Events seen
// by `watched` are reported through `seen`.
template <typename Done>
bool pumpUntil(GatedTransport& host, GatedTransport& watched, TransportEventType wanted, Done done,
               GatedTransport* other = nullptr)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    bool seen = false;
    while (std::chrono::steady_clock::now() < deadline) {
        TransportEvent event;
        while (host.poll(event)) {
        }
        while (watched.poll(event)) seen = seen || event.type == wanted;
        if (other != nullptr)
            while (other->poll(event)) {
            }
        if (seen && done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

} // namespace

int main()
{
    const JoinCredentials room = credentials(1);
    JoinCredentials wrong = room;
    wrong.joinSecret[0] ^= 1;
    const Endpoint endpoint{"127.0.0.1", 39873};

    auto host = make(GatedTransport::Role::Host, room);
    check(host->listen(endpoint), "gated host listens");

    auto intruder = make(GatedTransport::Role::Guest, wrong);
    check(intruder->connect(endpoint), "intruder starts connecting");
    check(pumpUntil(*host, *intruder, TransportEventType::Error,
                    [&] { return host->rejectedPeers() == 1; }),
          "intruder with a wrong secret is refused over real UDP");
    check(intruder->lastJoinError() == JoinError::Refused, "intruder learns it was refused");
    check(host->state() == TransportState::Listening, "room stays open after the refusal");
    intruder.reset();

    auto guest = make(GatedTransport::Role::Guest, room);
    check(guest->connect(endpoint), "guest starts connecting");
    check(pumpUntil(*host, *guest, TransportEventType::Connected,
                    [&] { return host->state() == TransportState::Connected; }),
          "guest with the right secret authenticates");

    auto second = make(GatedTransport::Role::Guest, room);
    check(second->connect(endpoint), "second guest starts connecting");
    check(pumpUntil(*host, *second, TransportEventType::Disconnected,
                    [&] { return second->state() != TransportState::Connected; }, guest.get()),
          "second guest is refused while the room is full");
    check(host->state() == TransportState::Connected && guest->state() == TransportState::Connected,
          "first guest survives the second attempt");
    second.reset();

    // Quality stays -1 until GNS has measured it; RTT is known at once.
    TransportStats stats;
    check(host->stats(stats) && stats.rttMs >= 0
              && (stats.localQuality == -1.0f || (stats.localQuality >= 0.0f && stats.localQuality <= 1.0f)),
          "real link reports RTT and a valid quality");

    const std::uint8_t gameplay[] = {'p', 'i', 'k'};
    check(guest->send(Delivery::ReliableOrdered, gameplay, sizeof(gameplay)), "guest sends gameplay");
    check(pumpUntil(*guest, *host, TransportEventType::Message, [] { return true; }),
          "host receives gameplay after authentication");

    guest->disconnectPeer(true);
    check(pumpUntil(*guest, *host, TransportEventType::Disconnected,
                    [&] { return host->state() == TransportState::Listening; }),
          "host sees the guest leave and keeps listening");

    auto again = make(GatedTransport::Role::Guest, room);
    check(again->connect(endpoint), "guest reconnects");
    check(pumpUntil(*host, *again, TransportEventType::Connected,
                    [&] { return host->state() == TransportState::Connected; }),
          "reconnecting guest authenticates again");

    again->close();
    host->close();

    // Compatibility over real UDP: a PAL guest is told why, then a matching
    // guest gets in through the same room.
    CompatibilityInfo usa;
    usa.game = static_cast<std::uint8_t>(GameId::Pikmin1);
    usa.region = 1;
    CompatibilityInfo pal = usa;
    pal.region = 2;
    const Endpoint compatEndpoint{"127.0.0.1", 39874};
    auto compatRoom = make(GatedTransport::Role::Host, credentials(1));
    compatRoom->requireCompatibility(usa);
    check(compatRoom->listen(compatEndpoint), "compat room listens");
    auto palGuest = make(GatedTransport::Role::Guest, credentials(1));
    palGuest->requireCompatibility(pal);
    palGuest->connect(compatEndpoint);
    bool explained = false;
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!explained && std::chrono::steady_clock::now() < until) {
        TransportEvent event;
        while (compatRoom->poll(event)) {
        }
        while (palGuest->poll(event)) explained = explained || event.detail == "incompatible:region";
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    check(explained, "incompatible guest is told the region differs over real UDP");
    palGuest.reset();
    auto usaGuest = make(GatedTransport::Role::Guest, credentials(1));
    usaGuest->requireCompatibility(usa);
    usaGuest->connect(compatEndpoint);
    check(pumpUntil(*compatRoom, *usaGuest, TransportEventType::Connected,
                    [&] { return compatRoom->state() == TransportState::Connected; }),
          "compatible guest joins the same room afterwards");
    check(compatRoom->bannedOrigins() == 0, "incompatible attempt is not penalised");
    usaGuest->close();
    compatRoom->close();
    std::printf("nectar_net_gated_gns_test: %s\n", failures == 0 ? "all tests passed" : "FAILED");
    return failures == 0 ? 0 : 1;
}
