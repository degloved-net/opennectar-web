// The one-code room over real loopback UDP with GNS. Automatic port mapping
// is off so the test never touches the router; the code carries 127.0.0.1.
#include "nectar_net/room.h"

#include <chrono>
#include <cstdio>
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

std::uint64_t nowMs()
{
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch())
                                          .count());
}

CompatibilityInfo game(std::uint8_t region)
{
    CompatibilityInfo info;
    info.game = static_cast<std::uint8_t>(GameId::Pikmin2);
    info.region = region;
    return info;
}

JoinProgress settle(HostRoom& host, GuestJoin& guest)
{
    const std::uint64_t until = nowMs() + 15000;
    JoinProgress progress = JoinProgress::Connecting;
    while (nowMs() < until) {
        TransportEvent event;
        while (host.transport()->poll(event)) {
        }
        bool has = false;
        progress = guest.poll(nowMs(), event, has);
        if (progress != JoinProgress::Connecting) return progress;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return progress;
}

} // namespace

int main()
{
    constexpr std::uint64_t kNow = 1790000000U;
    RoomConfig config;
    config.compatibility = game(1);
    config.localPort = 39886;
    config.automaticPortMapping = false;
    config.includeLanEndpoint = false;
    DirectEndpoint loopback;
    loopback.address = {127, 0, 0, 1};
    loopback.port = config.localPort;
    config.manualEndpoints.push_back(loopback);

    HostRoom host;
    RoomStatus status;
    check(host.open(config, kNow, status) == RoomError::None && !status.code.empty(), "room opens over GNS");

    GuestJoin guest;
    check(guest.start(status.code, game(1), 1, kNow, nowMs()) == JoinFailure::None, "guest starts");
    check(settle(host, guest) == JoinProgress::Connected, "guest joins over real UDP with one code");

    GuestJoin second;
    second.start(status.code, game(1), 1, kNow, nowMs());
    const JoinProgress full = settle(host, second);
    check(full == JoinProgress::Failed && second.failure() == JoinFailure::RoomFull,
          "GNS reports a full room to a second guest");
    if (second.failure() != JoinFailure::RoomFull) std::printf("second guest: %s\n", joinFailureString(second.failure()));

    guest.cancel();
    TransportEvent event;
    const std::uint64_t until = nowMs() + 5000;
    while (host.transport()->state() != TransportState::Listening && nowMs() < until) {
        while (host.transport()->poll(event)) {
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    GuestJoin pal;
    pal.start(status.code, game(2), 1, kNow, nowMs());
    check(settle(host, pal) == JoinProgress::Failed && pal.failure() == JoinFailure::Incompatible
              && pal.incompatibility() == CompatibilityField::Region,
          "an incompatible guest is told why over real UDP");

    host.close();
    std::printf("nectar_net_room_gns_test: %s\n", failures == 0 ? "all tests passed" : "FAILED");
    return failures == 0 ? 0 : 1;
}
