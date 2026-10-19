#include "nectar_net/room.h"

#include <cstdio>
#include <deque>
#include <map>
#include <memory>
#include <set>
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

// In-memory network: listeners by port; some addresses black-hole traffic.
struct FakeEnd;
struct FakeNet {
    std::map<std::uint16_t, FakeEnd*> listeners;
    std::set<std::string> deadAddresses;
};

struct FakeEnd final : ITransport {
    FakeNet& net;
    FakeEnd* peer = nullptr;
    std::uint16_t listening = 0;
    TransportState current = TransportState::Closed;
    std::deque<TransportEvent> inbox;

    explicit FakeEnd(FakeNet& n) : net(n) {}
    ~FakeEnd() override { close(); }

    void push(TransportEventType type, const std::string& detail = {}, Delivery delivery = Delivery::ReliableOrdered,
              const std::uint8_t* data = nullptr, std::size_t size = 0)
    {
        TransportEvent event;
        event.type = type;
        event.detail = detail;
        event.delivery = delivery;
        if (data != nullptr) event.payload.assign(data, data + size);
        inbox.push_back(std::move(event));
    }

    bool listen(const Endpoint& endpoint) override
    {
        if (net.listeners.count(endpoint.port) != 0) return false;
        net.listeners[endpoint.port] = this;
        listening = endpoint.port;
        current = TransportState::Listening;
        return true;
    }
    bool connect(const Endpoint& endpoint) override
    {
        current = TransportState::Connecting;
        if (net.deadAddresses.count(endpoint.address) != 0) return true; // never answers
        auto it = net.listeners.find(endpoint.port);
        if (it == net.listeners.end()) {
            push(TransportEventType::Disconnected, "no route");
            current = TransportState::Closed;
            return true;
        }
        FakeEnd* host = it->second;
        if (host->peer != nullptr) {
            push(TransportEventType::Disconnected, kRoomFullReason);
            current = TransportState::Closed;
            return true;
        }
        peer = host;
        host->peer = this;
        current = host->current = TransportState::Connected;
        push(TransportEventType::Connected);
        host->push(TransportEventType::Connected);
        host->inbox.back().peerAddress = "192.0.2.50";
        return true;
    }
    bool send(Delivery delivery, const std::uint8_t* data, std::size_t size) override
    {
        if (current != TransportState::Connected || peer == nullptr) return false;
        peer->push(TransportEventType::Message, {}, delivery, data, size);
        return true;
    }
    bool poll(TransportEvent& event) override
    {
        if (inbox.empty()) return false;
        event = std::move(inbox.front());
        inbox.pop_front();
        return true;
    }
    void disconnectPeer(bool) override
    {
        if (peer != nullptr) {
            peer->push(TransportEventType::Disconnected, "peer left");
            peer->current = peer->listening ? TransportState::Listening : TransportState::Closed;
            peer->peer = nullptr;
            peer = nullptr;
        }
        current = listening ? TransportState::Listening : TransportState::Closed;
    }
    void close() override
    {
        disconnectPeer(false);
        if (listening) net.listeners.erase(listening);
        listening = 0;
        current = TransportState::Closed;
    }
    TransportState state() const override { return current; }
    bool stats(TransportStats& output) const override
    {
        output = TransportStats{};
        return current == TransportState::Connected;
    }
};

TransportFactory factory(FakeNet& net)
{
    return [&net]() -> std::unique_ptr<ITransport> { return std::make_unique<FakeEnd>(net); };
}

PublisherBackends lanOnly()
{
    PublisherBackends b;
    b.listAddresses = [] {
        LocalAddresses local;
        local.ipv4.push_back({192, 168, 1, 23});
        return local;
    };
    b.randomBytes = [](std::uint8_t* out, std::size_t size) {
        for (std::size_t i = 0; i < size; ++i) out[i] = static_cast<std::uint8_t>(i + 7);
        return true;
    };
    return b;
}

CompatibilityInfo game(std::uint8_t region)
{
    CompatibilityInfo info;
    info.game = static_cast<std::uint8_t>(GameId::Pikmin1);
    info.region = region;
    return info;
}

RoomConfig config(std::uint8_t region = 1)
{
    RoomConfig c;
    c.compatibility = game(region);
    c.localPort = 45000;
    return c;
}

constexpr std::uint64_t kNow = 1790000000U;

// Pumps host and guest until the guest settles or the clock runs out.
JoinProgress run(HostRoom& host, GuestJoin& guest, std::uint64_t& clockMs, int rounds = 50)
{
    JoinProgress progress = JoinProgress::Connecting;
    for (int i = 0; i < rounds; ++i) {
        TransportEvent event;
        if (host.transport() != nullptr)
            while (host.transport()->poll(event)) {
            }
        bool has = false;
        progress = guest.poll(clockMs, event, has);
        if (progress != JoinProgress::Connecting) return progress;
        clockMs += 1000;
    }
    return progress;
}

void testHappyPath()
{
    FakeNet net;
    HostRoom host(factory(net), lanOnly());
    RoomStatus status;
    check(host.open(config(), kNow, status) == RoomError::None, "host opens a room");
    check(status.code.rfind("NECTAR1-", 0) == 0 && status.localPort == 45000, "room shows a code");
    check(status.reachability == Reachability::NoAutomaticMapping, "LAN-only room says it is not public");

    GuestJoin guest(factory(net));
    std::uint64_t clock = 0;
    check(guest.start(status.code, game(1), 1, kNow + 10, clock) == JoinFailure::None, "guest starts");
    check(run(host, guest, clock) == JoinProgress::Connected, "guest joins with one code");
    TransportEvent event;
    bool hostSawGuest = false;
    while (host.transport()->poll(event)) hostSawGuest = hostSawGuest || event.type == TransportEventType::Connected;
    check(host.transport()->state() == TransportState::Connected, "host sees the guest connected");

    const std::uint8_t hello = 7;
    check(guest.transport()->send(Delivery::ReliableOrdered, &hello, 1), "guest can send gameplay");
    bool got = false;
    while (host.transport()->poll(event)) got = got || (event.type == TransportEventType::Message);
    check(got, "host receives it");

    host.close();
    bool has = false;
    check(guest.poll(clock, event, has) == JoinProgress::Failed && guest.failure() == JoinFailure::Lost,
          "guest learns that the host left");
}

void testDeadEndpointIsSkipped()
{
    FakeNet net;
    net.deadAddresses.insert("81.32.10.20");
    HostRoom host(factory(net), lanOnly());
    RoomConfig c = config();
    DirectEndpoint manual;
    manual.address = {81, 32, 10, 20};
    manual.port = 45000;
    c.manualEndpoints.push_back(manual);
    RoomStatus status;
    check(host.open(c, kNow, status) == RoomError::None, "room with a manual endpoint");
    GuestJoin guest(factory(net));
    std::uint64_t clock = 0;
    guest.start(status.code, game(1), 1, kNow, clock);
    check(run(host, guest, clock) == JoinProgress::Connected && guest.attemptedEndpoints() == 2,
          "a silent endpoint times out and the next one is tried");
    check(clock >= GuestJoin::kAttemptTimeoutMs, "the silent endpoint got its full deadline");
}

void testBadCodes()
{
    FakeNet net;
    HostRoom host(factory(net), lanOnly());
    RoomStatus status;
    host.open(config(), kNow, status);
    GuestJoin guest(factory(net));
    std::string typo = status.code;
    typo[12] = typo[12] == 'A' ? 'B' : 'A';
    check(guest.start(typo, game(1), 1, kNow, 0) == JoinFailure::BadCode, "typo is a bad code");
    check(guest.start("hello", game(1), 1, kNow, 0) == JoinFailure::BadCode, "garbage is a bad code");
    check(guest.start(status.code, game(1), 1, kNow + 3 * 60 * 60, 0) == JoinFailure::Expired, "old code expired");
    check(guest.start(status.code, game(1), 2, kNow, 0) == JoinFailure::WrongVersion, "other protocol version");
}

void testIncompatible()
{
    FakeNet net;
    HostRoom host(factory(net), lanOnly());
    RoomStatus status;
    host.open(config(1), kNow, status);
    GuestJoin guest(factory(net));
    std::uint64_t clock = 0;
    guest.start(status.code, game(2), 1, kNow, clock);
    check(run(host, guest, clock) == JoinProgress::Failed && guest.failure() == JoinFailure::Incompatible,
          "a guest with another region is refused as incompatible");
    check(guest.incompatibility() == CompatibilityField::Region && guest.host() != nullptr && guest.host()->region == 1,
          "the guest can explain which region the host uses");
}

void testStaleRoomAndFullRoom()
{
    FakeNet net;
    HostRoom first(factory(net), lanOnly());
    RoomStatus old;
    first.open(config(), kNow, old);
    first.close();
    HostRoom host(factory(net), lanOnly());
    RoomStatus status;
    host.open(config(), kNow, status);
    GuestJoin stale(factory(net));
    std::uint64_t clock = 0;
    stale.start(old.code, game(1), 1, kNow, clock);
    check(run(host, stale, clock) == JoinProgress::Failed && stale.failure() == JoinFailure::Refused,
          "a code from a closed room is refused by the new room on that port");

    GuestJoin guest(factory(net));
    clock = 0;
    guest.start(status.code, game(1), 1, kNow, clock);
    check(run(host, guest, clock) == JoinProgress::Connected, "first guest joins");
    GuestJoin third(factory(net));
    clock = 0;
    third.start(status.code, game(1), 1, kNow, clock);
    check(run(host, third, clock) == JoinProgress::Failed && third.failure() == JoinFailure::RoomFull,
          "a second guest is told the room is full");
}

void testClosedRoom()
{
    FakeNet net;
    HostRoom host(factory(net), lanOnly());
    RoomStatus status;
    host.open(config(), kNow, status);
    host.close();
    GuestJoin guest(factory(net));
    std::uint64_t clock = 0;
    guest.start(status.code, game(1), 1, kNow, clock);
    check(run(host, guest, clock) == JoinProgress::Failed && guest.failure() == JoinFailure::Unreachable,
          "closing the room invalidates the code");
}

void testHostErrors()
{
    FakeNet net;
    HostRoom busy(factory(net), lanOnly());
    RoomStatus status;
    busy.open(config(), kNow, status);
    HostRoom second(factory(net), lanOnly());
    check(second.open(config(), kNow, status) == RoomError::ListenFailed, "a fixed port in use is reported");
    RoomConfig random = config();
    random.localPort = 0;
    check(second.open(random, kNow, status) == RoomError::None && status.localPort >= 49152,
          "a random high port is chosen otherwise");
    PublisherBackends nothing;
    HostRoom isolated(factory(net), nothing);
    RoomConfig other = config();
    other.localPort = 45001;
    check(isolated.open(other, kNow, status) == RoomError::NoEndpoint && status.code.empty(),
          "a room with nowhere to be reached shows no code");
    HostRoom noTransport([]() -> std::unique_ptr<ITransport> { return nullptr; }, lanOnly());
    check(noTransport.open(config(), kNow, status) == RoomError::NoTransport, "missing transport is reported");
}

} // namespace

int main()
{
    testHappyPath();
    testDeadEndpointIsSkipped();
    testBadCodes();
    testIncompatible();
    testStaleRoomAndFullRoom();
    testClosedRoom();
    testHostErrors();
    std::printf("nectar_net_room_test: %s\n", failures == 0 ? "all tests passed" : "FAILED");
    return failures == 0 ? 0 : 1;
}
