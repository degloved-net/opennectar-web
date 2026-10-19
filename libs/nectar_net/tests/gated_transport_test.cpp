#include "nectar_net/gated_transport.h"
#include "nectar_net/protocol.h"

#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <string>

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

// In-memory peer pair. Delivery is immediate and lossless; the gate under test
// is what matters here, the real UDP path is covered by the GNS test.
struct FakeEnd;

struct FakeLink {
    FakeEnd* host = nullptr;
    FakeEnd* guest = nullptr;
};

struct FakeEnd final : ITransport {
    std::shared_ptr<FakeLink> link;
    bool isHost;
    TransportState current = TransportState::Closed;
    std::deque<TransportEvent> inbox;
    int disconnects = 0;
    bool lastFlush = false;
    std::string address = "192.0.2.10"; // what the host sees for this peer

    FakeEnd(std::shared_ptr<FakeLink> shared, bool host) : link(std::move(shared)), isHost(host)
    {
        (isHost ? link->host : link->guest) = this;
    }
    ~FakeEnd() override { (isHost ? link->host : link->guest) = nullptr; }

    FakeEnd* peer() const { return isHost ? link->guest : link->host; }
    void push(TransportEventType type, Delivery delivery = Delivery::ReliableOrdered,
              const std::uint8_t* data = nullptr, std::size_t size = 0)
    {
        TransportEvent event;
        event.type = type;
        event.delivery = delivery;
        if (data != nullptr) event.payload.assign(data, data + size);
        inbox.push_back(std::move(event));
    }

    bool listen(const Endpoint&) override
    {
        current = TransportState::Listening;
        return true;
    }
    bool connect(const Endpoint&) override
    {
        FakeEnd* other = peer();
        if (other == nullptr || other->current != TransportState::Listening) return false;
        current = other->current = TransportState::Connected;
        push(TransportEventType::Connected);
        other->push(TransportEventType::Connected);
        other->inbox.back().peerAddress = address;
        return true;
    }
    bool send(Delivery delivery, const std::uint8_t* data, std::size_t size) override
    {
        FakeEnd* other = peer();
        if (current != TransportState::Connected || other == nullptr) return false;
        other->push(TransportEventType::Message, delivery, data, size);
        return true;
    }
    bool poll(TransportEvent& event) override
    {
        if (inbox.empty()) return false;
        event = std::move(inbox.front());
        inbox.pop_front();
        return true;
    }
    void disconnectPeer(bool flush) override
    {
        ++disconnects;
        lastFlush = flush;
        if (current != TransportState::Connected) return;
        current = isHost ? TransportState::Listening : TransportState::Closed;
        if (FakeEnd* other = peer()) {
            other->current = other->isHost ? TransportState::Listening : TransportState::Closed;
            other->push(TransportEventType::Disconnected);
        }
    }
    void close() override
    {
        disconnectPeer(false);
        current = TransportState::Closed;
    }
    TransportState state() const override { return current; }
    bool stats(TransportStats& output) const override
    {
        output = TransportStats{};
        if (current != TransportState::Connected) return false;
        output.rttMs = 42;
        return true;
    }
};

JoinCredentials credentials(std::uint8_t seed)
{
    JoinCredentials value;
    for (std::size_t i = 0; i < 16; ++i) {
        value.sessionNonce[i] = static_cast<std::uint8_t>(seed + i);
        value.joinSecret[i] = static_cast<std::uint8_t>(seed + 0x80U + i);
    }
    return value;
}

struct Pair {
    std::shared_ptr<FakeLink> link = std::make_shared<FakeLink>();
    std::uint64_t now = 0;
    FakeEnd* hostEnd = nullptr;
    FakeEnd* guestEnd = nullptr;
    std::unique_ptr<GatedTransport> host;
    std::unique_ptr<GatedTransport> guest;

    Pair(const JoinCredentials& hostCreds, const JoinCredentials& guestCreds)
    {
        auto clock = [this] { return now; };
        auto h = std::make_unique<FakeEnd>(link, true);
        auto g = std::make_unique<FakeEnd>(link, false);
        hostEnd = h.get();
        guestEnd = g.get();
        host = std::make_unique<GatedTransport>(std::move(h), GatedTransport::Role::Host, hostCreds, clock, 5000);
        guest = std::make_unique<GatedTransport>(std::move(g), GatedTransport::Role::Guest, guestCreds, clock, 5000);
        host->listen({"127.0.0.1", 1});
    }

    // Pumps both sides until quiet; returns the events each side surfaced.
    void pump(std::deque<TransportEvent>& hostEvents, std::deque<TransportEvent>& guestEvents)
    {
        for (int round = 0; round < 16; ++round) {
            TransportEvent event;
            bool any = false;
            while (host->poll(event)) hostEvents.push_back(std::move(event)), any = true;
            while (guest->poll(event)) guestEvents.push_back(std::move(event)), any = true;
            if (!any && hostEnd->inbox.empty() && guestEnd->inbox.empty()) return;
        }
    }
};

bool has(const std::deque<TransportEvent>& events, TransportEventType type)
{
    for (const TransportEvent& event : events)
        if (event.type == type) return true;
    return false;
}

void testAuthenticatedSession()
{
    const JoinCredentials creds = credentials(1);
    Pair pair(creds, creds);
    const std::uint8_t early[] = {1};
    check(!pair.guest->send(Delivery::ReliableOrdered, early, 1), "guest cannot send before connecting");
    check(pair.guest->connect({"127.0.0.1", 1}), "guest connects");
    check(pair.host->state() == TransportState::Connecting, "host reports connecting during quarantine");
    check(!pair.host->send(Delivery::ReliableOrdered, early, 1), "host cannot send gameplay during quarantine");

    std::deque<TransportEvent> hostEvents, guestEvents;
    pair.pump(hostEvents, guestEvents);
    check(has(hostEvents, TransportEventType::Connected), "host surfaces Connected after the secret");
    check(has(guestEvents, TransportEventType::Connected), "guest surfaces Connected after acceptance");
    check(!has(hostEvents, TransportEventType::Message) && !has(guestEvents, TransportEventType::Message),
          "handshake messages never reach the session layer");
    check(pair.host->state() == TransportState::Connected && pair.guest->state() == TransportState::Connected,
          "both sides connected");

    const std::uint8_t gameplay[] = {'g', 'o'};
    check(pair.guest->send(Delivery::Unreliable, gameplay, sizeof(gameplay)), "guest sends gameplay");
    hostEvents.clear();
    guestEvents.clear();
    pair.pump(hostEvents, guestEvents);
    check(hostEvents.size() == 1 && hostEvents[0].type == TransportEventType::Message
              && hostEvents[0].payload.size() == 2 && hostEvents[0].payload[0] == 'g',
          "host receives gameplay after authentication");

    pair.now += 60000;
    hostEvents.clear();
    pair.pump(hostEvents, guestEvents);
    check(pair.host->state() == TransportState::Connected, "no timeout once authenticated");

    pair.guest->disconnectPeer(false);
    hostEvents.clear();
    pair.pump(hostEvents, guestEvents);
    check(has(hostEvents, TransportEventType::Disconnected), "authenticated guest leaving is surfaced");
    check(pair.host->state() == TransportState::Listening, "host keeps the room open");
}

void testWrongSecretKeepsRoomOpen()
{
    const JoinCredentials good = credentials(2);
    JoinCredentials bad = good;
    bad.joinSecret[3] ^= 0x10;
    Pair pair(good, bad);
    pair.guest->connect({"127.0.0.1", 1});
    std::deque<TransportEvent> hostEvents, guestEvents;
    pair.pump(hostEvents, guestEvents);

    check(hostEvents.empty(), "rejected guest is invisible to the host session");
    check(pair.host->rejectedPeers() == 1 && pair.host->lastJoinError() == JoinError::Refused,
          "host counts the refusal");
    check(pair.hostEnd->lastFlush, "refusal is flushed before dropping the peer");
    check(pair.host->state() == TransportState::Listening, "host keeps listening after a refusal");
    check(has(guestEvents, TransportEventType::Error) && !has(guestEvents, TransportEventType::Connected),
          "guest reports the refusal and never connects");
    bool sawRefused = false;
    for (const TransportEvent& event : guestEvents) sawRefused = sawRefused || event.detail == "refused";
    check(sawRefused, "guest error says refused");
}

void testSilentPeerTimesOut()
{
    const JoinCredentials creds = credentials(3);
    Pair pair(creds, creds);
    // Raw connection without a gate: simulates a scanner that never speaks.
    auto scanner = std::make_unique<FakeEnd>(pair.link, false);
    pair.guestEnd = scanner.get();
    scanner->connect({"127.0.0.1", 1});
    std::deque<TransportEvent> hostEvents, guestEvents;
    TransportEvent event;
    while (pair.host->poll(event)) hostEvents.push_back(event);
    check(pair.host->state() == TransportState::Connecting, "silent peer stays quarantined");
    pair.now += 4999;
    check(!pair.host->poll(event) && pair.host->rejectedPeers() == 0, "host waits for the deadline");
    pair.now += 1;
    pair.host->poll(event);
    check(pair.host->rejectedPeers() == 1 && pair.host->lastJoinError() == JoinError::Timeout,
          "silent peer is dropped at the deadline");
    check(pair.host->state() == TransportState::Listening && hostEvents.empty(),
          "timeout is invisible and the room stays open");
}

void testGameplayBeforeSecretIsDropped()
{
    const JoinCredentials creds = credentials(4);
    Pair pair(creds, creds);
    auto rogue = std::make_unique<FakeEnd>(pair.link, false);
    rogue->connect({"127.0.0.1", 1});
    Message ping;
    ping.header.type = MessageType::Ping;
    std::vector<std::uint8_t> bytes;
    encodeMessage(ping, bytes);
    rogue->send(Delivery::ReliableOrdered, bytes.data(), bytes.size());

    TransportEvent event;
    bool surfaced = false;
    while (pair.host->poll(event)) surfaced = true;
    check(!surfaced, "gameplay before the secret is never surfaced");
    check(pair.host->rejectedPeers() == 1 && pair.host->lastJoinError() == JoinError::UnexpectedMessage,
          "peer speaking out of turn is dropped");
    check(!pair.hostEnd->lastFlush, "out-of-turn peer gets no reply");
}

// A peer that keeps failing is banned, the ban doubles, and the right guest
// from another origin still gets in while the ban runs.
void testRepeatedFailuresAreBanned()
{
    const JoinCredentials good = credentials(6);
    JoinCredentials bad = good;
    bad.joinSecret[0] ^= 1;
    Pair pair(good, good);
    std::deque<TransportEvent> hostEvents, guestEvents;

    auto attempt = [&](const JoinCredentials& creds, const char* address) {
        auto end = std::make_unique<FakeEnd>(pair.link, false);
        end->address = address;
        GatedTransport peer(std::move(end), GatedTransport::Role::Guest, creds, [&] { return pair.now; }, 5000);
        peer.connect({"127.0.0.1", 1});
        for (int round = 0; round < 8; ++round) {
            TransportEvent event;
            while (pair.host->poll(event)) hostEvents.push_back(event);
            while (peer.poll(event)) {
            }
        }
        const bool connected = pair.host->state() == TransportState::Connected;
        if (connected) pair.host->disconnectPeer(false);
        TransportEvent event;
        while (pair.host->poll(event)) {
        }
        return connected;
    };

    for (std::uint32_t i = 0; i < GatedTransport::kFailuresBeforeBan; ++i) attempt(bad, "198.51.100.7");
    check(pair.host->bannedOrigins() == 1, "origin is banned after repeated failures");
    const std::uint32_t before = pair.host->rejectedPeers();
    check(!attempt(good, "198.51.100.7"), "banned origin is dropped even with the right secret");
    check(pair.host->lastJoinError() == JoinError::RateLimited && pair.host->rejectedPeers() == before + 1,
          "ban is reported as rate_limited");
    check(attempt(good, "203.0.113.5"), "another origin still joins while the ban runs");

    pair.now += GatedTransport::kFirstBanMs;
    check(pair.host->bannedOrigins() == 0, "ban expires");
    attempt(bad, "198.51.100.7");
    check(pair.host->bannedOrigins() == 1, "one more failure re-bans at once");
    pair.now += GatedTransport::kFirstBanMs;
    check(pair.host->bannedOrigins() == 1, "second ban lasts twice as long");
    pair.now += GatedTransport::kFirstBanMs;
    check(attempt(good, "198.51.100.7"), "origin joins once the ban is over");
    attempt(bad, "198.51.100.7");
    check(pair.host->bannedOrigins() == 0, "a success resets the failure count");
}

void testOriginTableIsBounded()
{
    const JoinCredentials good = credentials(7);
    JoinCredentials bad = good;
    bad.joinSecret[0] ^= 1;
    Pair pair(good, good);
    char address[32];
    for (std::size_t i = 0; i < GatedTransport::kMaxTrackedOrigins * 2; ++i) {
        std::snprintf(address, sizeof(address), "10.0.%zu.%zu", i / 200, i % 200);
        for (std::uint32_t n = 0; n < GatedTransport::kFailuresBeforeBan; ++n) {
            auto end = std::make_unique<FakeEnd>(pair.link, false);
            end->address = address;
            GatedTransport peer(std::move(end), GatedTransport::Role::Guest, bad, [&] { return pair.now; }, 5000);
            peer.connect({"127.0.0.1", 1});
            TransportEvent event;
            for (int round = 0; round < 6; ++round) {
                while (pair.host->poll(event)) {
                }
                while (peer.poll(event)) {
                }
            }
        }
    }
    check(pair.host->bannedOrigins() <= GatedTransport::kMaxTrackedOrigins, "origin table stays bounded");
}

void testStatsPassThrough()
{
    const JoinCredentials creds = credentials(8);
    Pair pair(creds, creds);
    TransportStats stats;
    check(!pair.host->stats(stats) && stats.rttMs == -1, "no stats without a peer");
    pair.guest->connect({"127.0.0.1", 1});
    std::deque<TransportEvent> hostEvents, guestEvents;
    pair.pump(hostEvents, guestEvents);
    check(pair.host->stats(stats) && stats.rttMs == 42, "stats come from the inner transport");
}

CompatibilityInfo compatInfo(std::uint8_t region)
{
    CompatibilityInfo info;
    info.game = static_cast<std::uint8_t>(GameId::Pikmin1);
    info.region = region;
    info.build[0] = 0x42;
    return info;
}

void testCompatibilityStep()
{
    const JoinCredentials creds = credentials(9);
    {
        Pair pair(creds, creds);
        pair.host->requireCompatibility(compatInfo(1));
        pair.guest->requireCompatibility(compatInfo(1));
        pair.guest->connect({"127.0.0.1", 1});
        std::deque<TransportEvent> hostEvents, guestEvents;
        pair.pump(hostEvents, guestEvents);
        check(has(hostEvents, TransportEventType::Connected) && has(guestEvents, TransportEventType::Connected),
              "compatible pair connects after both steps");
        check(!has(hostEvents, TransportEventType::Message) && !has(guestEvents, TransportEventType::Message),
              "compatibility messages never reach the session layer");
        const std::uint8_t byte = 1;
        check(pair.guest->send(Delivery::Unreliable, &byte, 1), "gameplay flows once compatible");
    }
    {
        Pair pair(creds, creds);
        pair.host->requireCompatibility(compatInfo(1));
        pair.guest->requireCompatibility(compatInfo(2));
        for (int attempt = 0; attempt < 5; ++attempt) {
            pair.guest->connect({"127.0.0.1", 1});
            std::deque<TransportEvent> hostEvents, guestEvents;
            pair.pump(hostEvents, guestEvents);
            bool explained = false;
            for (const TransportEvent& event : guestEvents) explained = explained || event.detail == "incompatible:region";
            check(explained && !has(guestEvents, TransportEventType::Connected),
                  "guest is told the region differs and never connects");
            check(hostEvents.empty() && pair.host->state() == TransportState::Listening,
                  "host stays open and silent for an incompatible guest");
            check(pair.guest->compatibility()->peer().region == 1, "guest knows the host's region");
            const std::uint8_t byte = 1;
            check(!pair.host->send(Delivery::Unreliable, &byte, 1), "no gameplay with an incompatible guest");
        }
        check(pair.host->bannedOrigins() == 0, "an incompatible friend is never banned");
    }
}

void testRoleMisuse()
{
    const JoinCredentials creds = credentials(5);
    Pair pair(creds, creds);
    check(!pair.host->connect({"127.0.0.1", 1}), "host role cannot connect");
    check(!pair.guest->listen({"127.0.0.1", 1}), "guest role cannot listen");
}

} // namespace

int main()
{
    testAuthenticatedSession();
    testWrongSecretKeepsRoomOpen();
    testSilentPeerTimesOut();
    testGameplayBeforeSecretIsDropped();
    testRepeatedFailuresAreBanned();
    testOriginTableIsBounded();
    testStatsPassThrough();
    testCompatibilityStep();
    testRoleMisuse();

    std::printf("nectar_net_gated_transport_test: %s\n", failures == 0 ? "all tests passed" : "FAILED");
    return failures == 0 ? 0 : 1;
}
