// Phase 4 gate, transport side: 30 simulated minutes at RTT 150 ms, jitter
// 30 ms and 5% loss, through the real join quarantine and compatibility check.
// The guest sends its input every tick; the host simulates with a fixed input
// delay. Checks: almost every tick arrives live, no press or release is lost
// or doubled, the clock estimate holds, queues stay bounded, and the recorded
// session replays to the very same input stream.
#include "nectar_net/clock.h"
#include "nectar_net/gated_transport.h"
#include "nectar_net/input.h"
#include "nectar_net/protocol.h"
#include "nectar_net/recording.h"
#include "nectar_net/sim_network.h"

#include <cstdio>
#include <cstdlib>
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

constexpr std::uint16_t kA = 0x0100;

JoinCredentials creds()
{
    JoinCredentials c;
    for (std::size_t i = 0; i < 16; ++i) {
        c.sessionNonce[i] = static_cast<std::uint8_t>(i + 1);
        c.joinSecret[i] = static_cast<std::uint8_t>(0x77 + i);
    }
    return c;
}

std::vector<std::uint8_t> wrap(MessageType type, std::uint64_t tick, const std::vector<std::uint8_t>& payload)
{
    Message message;
    message.header.type = type;
    message.header.simulationTick = tick;
    message.payload = payload;
    std::vector<std::uint8_t> out;
    encodeMessage(message, out);
    return out;
}

std::uint64_t tickAt(std::uint64_t ms) { return ms * kTicksPerSecond / 1000; }

// Guest player: taps A for 2-6 ticks every so often, deterministic.
struct Player {
    std::uint64_t rng = 99;
    std::uint64_t releaseAt = 0;
    std::uint64_t nextPressAt = 10;
    int presses = 0;
    NetInputFrame frame(std::uint64_t tick, std::uint16_t previousHeld)
    {
        rng ^= rng << 13;
        rng ^= rng >> 7;
        rng ^= rng << 17;
        NetInputFrame f;
        f.tick = tick;
        f.stickX = static_cast<std::int8_t>(static_cast<int>(rng % 200) - 100);
        f.held = previousHeld;
        if (tick == nextPressAt) {
            f.held = kA;
            f.pressed = kA;
            ++presses;
            releaseAt = tick + 2 + rng % 5;
        } else if (tick == releaseAt) {
            f.held = 0;
            f.released = kA;
            nextPressAt = tick + 3 + rng % 20;
        }
        return f;
    }
};

struct Consumed {
    std::vector<NetInputFrame> frames;
    int presses = 0;
    int releases = 0;
    std::uint64_t live = 0;
    std::uint64_t total = 0;
};

void consumeInto(Consumed& out, InputReceiver& receiver, std::uint64_t tick)
{
    InputHealth health;
    const NetInputFrame f = receiver.consume(tick, health);
    out.frames.push_back(f);
    out.presses += (f.pressed & kA) ? 1 : 0;
    out.releases += (f.released & kA) ? 1 : 0;
    out.live += health == InputHealth::Live ? 1 : 0;
    ++out.total;
}

bool takeInput(const TransportEvent& event, std::vector<NetInputFrame>& frames)
{
    Message message;
    if (event.type != TransportEventType::Message
        || decodeMessage(event.payload.data(), event.payload.size(), message, 256) != DecodeError::None
        || message.header.type != MessageType::InputBundle) {
        return false;
    }
    return decodeInputBundle(message.payload.data(), message.payload.size(), InputLimits{}, frames);
}

} // namespace

int main()
{
    const std::uint64_t kMinutes = std::getenv("NECTAR_SOAK_MINUTES") ? std::strtoull(std::getenv("NECTAR_SOAK_MINUTES"), nullptr, 10) : 30;
    const std::uint64_t kDurationMs = kMinutes * 60 * 1000;
    constexpr std::uint64_t kInputDelayTicks = 4; // ~133 ms: above the worst one-way delay

    SimulatedNetwork net(2026);
    LinkConditions link;
    link.latencyMs = 60;
    link.jitterMs = 30;
    link.lossPerMille = 50;
    link.duplicatePerMille = 5;
    net.setGuestToHost(link);
    net.setHostToGuest(link);

    std::uint64_t now = 0;
    GatedTransport host(net.createHost(), GatedTransport::Role::Host, creds(), [&] { return now; });
    GatedTransport guest(net.createGuest(), GatedTransport::Role::Guest, creds(), [&] { return now; });
    CompatibilityInfo info;
    info.game = static_cast<std::uint8_t>(GameId::Pikmin1);
    host.requireCompatibility(info);
    guest.requireCompatibility(info);
    host.listen({"", 1});
    guest.connect({"", 1});

    // Handshake under the same bad link (reliable messages are never lost).
    bool hostUp = false, guestUp = false;
    for (; now < 5000 && !(hostUp && guestUp); now += 5) {
        net.advance(now);
        TransportEvent event;
        while (host.poll(event)) hostUp = hostUp || event.type == TransportEventType::Connected;
        while (guest.poll(event)) guestUp = guestUp || event.type == TransportEventType::Connected;
    }
    check(hostUp && guestUp, "handshake completes over a lossy, jittery link");

    Player player;
    std::vector<NetInputFrame> sent; // every guest frame, by tick
    std::uint16_t held = 0;
    InputReceiver receiver;
    Consumed live;
    SessionRecorder recorder;
    ClockEstimator clock;
    std::uint64_t guestTick = tickAt(now);
    std::uint64_t hostTick = tickAt(now);
    const std::uint64_t firstTick = guestTick;
    std::uint64_t worstClockError = 0;
    std::uint32_t pingId = 0;
    std::size_t maxBacklog = 0;

    const std::uint64_t endMs = now + kDurationMs;
    for (; now < endMs; now += 1) {
        net.advance(now);
        // Guest: one input bundle per tick, a ping every 500 ms.
        while (tickAt(now) > guestTick) {
            ++guestTick;
            const NetInputFrame f = player.frame(guestTick, held);
            held = f.held;
            if (sent.size() <= guestTick - firstTick) sent.resize(guestTick - firstTick + 1);
            sent[guestTick - firstTick] = f;
            std::vector<NetInputFrame> bundle;
            for (std::uint64_t i = 0; i < kMaxInputFrames && guestTick >= firstTick + i + 1; ++i)
                bundle.push_back(sent[guestTick - firstTick - i]);
            const auto bytes = wrap(MessageType::InputBundle, guestTick, encodeInputBundle(bundle));
            guest.send(Delivery::Unreliable, bytes.data(), bytes.size());
        }
        if (now % 500 == 0) {
            const auto ping = wrap(MessageType::Ping, 0, encodePing({++pingId, now}));
            guest.send(Delivery::Unreliable, ping.data(), ping.size());
        }

        // Host: receive, answer pings, simulate with the input delay.
        TransportEvent event;
        std::size_t backlog = 0;
        while (host.poll(event)) {
            ++backlog;
            if (event.type != TransportEventType::Message) continue;
            recorder.record(now, RecordDirection::Inbound, event.delivery, event.payload.data(), event.payload.size());
            std::vector<NetInputFrame> frames;
            if (takeInput(event, frames)) {
                receiver.receive(frames, hostTick);
                continue;
            }
            Message message;
            PingPayload ping;
            if (decodeMessage(event.payload.data(), event.payload.size(), message, 64) == DecodeError::None
                && message.header.type == MessageType::Ping
                && decodePing(message.payload.data(), message.payload.size(), ping)) {
                const std::uint64_t phase = now - hostTick * 1000 / kTicksPerSecond;
                const auto pong = wrap(MessageType::Pong, hostTick,
                                       encodePong({ping.id, ping.senderTimeMs, hostTick,
                                                   static_cast<std::uint32_t>(phase > 33 ? 33 : phase)}));
                host.send(Delivery::Unreliable, pong.data(), pong.size());
            }
        }
        maxBacklog = backlog > maxBacklog ? backlog : maxBacklog;
        while (tickAt(now) > hostTick) {
            ++hostTick;
            if (hostTick >= firstTick + kInputDelayTicks + 1) consumeInto(live, receiver, hostTick - kInputDelayTicks);
        }

        // Guest: clock samples.
        while (guest.poll(event)) {
            Message message;
            PongPayload pong;
            if (event.type == TransportEventType::Message
                && decodeMessage(event.payload.data(), event.payload.size(), message, 64) == DecodeError::None
                && message.header.type == MessageType::Pong
                && decodePong(message.payload.data(), message.payload.size(), pong)) {
                clock.addSample(pong, now);
            }
        }
        if (clock.ready() && now % 1000 == 0) {
            const std::uint64_t estimate = clock.hostTickAt(now);
            const std::uint64_t error = estimate > hostTick ? estimate - hostTick : hostTick - estimate;
            worstClockError = error > worstClockError ? error : worstClockError;
        }
    }

    const double liveRatio = live.total ? static_cast<double>(live.live) / static_cast<double>(live.total) : 0.0;
    std::printf("soak: %llu min, %llu ticks, live %.4f, presses sent %d / seen %d, releases seen %d, "
                "lost %llu, dup %llu, clock error <= %llu ticks, rtt %u ms, jitter %u ms, max backlog %zu\n",
                static_cast<unsigned long long>(kMinutes), static_cast<unsigned long long>(live.total), liveRatio,
                player.presses, live.presses, live.releases, static_cast<unsigned long long>(net.dropped()),
                static_cast<unsigned long long>(net.duplicated()), static_cast<unsigned long long>(worstClockError),
                clock.rttMs(), clock.jitterMs(), maxBacklog);
    // With 133 ms of input delay and up to 90 ms one way, only the bundles of
    // ticks T, T+1 and sometimes T+2 reach the host in time: about 2.2 copies,
    // so ~0.05^2.2 of ticks are expected to fall back to the held state.
    check(liveRatio > 0.995, "redundant input keeps more than 99.5% of ticks live at 5% loss");
    // The last presses may still be inside the input delay window.
    check(live.presses >= player.presses - 1 && live.presses <= player.presses, "no press lost or doubled");
    check(live.releases >= live.presses - 1 && live.releases <= live.presses, "every press is released once");
    check(worstClockError <= 1, "the clock estimate stays within one tick");
    check(maxBacklog < 64, "no queue builds up");
    check(!recorder.truncated(), "the session fits in the recording budget");

    // Replay the recorded inbound traffic and rebuild the same input stream.
    std::vector<RecordedMessage> messages;
    check(parseRecording(recorder.bytes().data(), recorder.bytes().size(), messages), "recording parses");
    ReplayTransport replay(messages);
    InputReceiver replayReceiver;
    Consumed replayed;
    std::uint64_t replayHostTick = firstTick;
    for (std::uint64_t t = 0; t <= endMs; ++t) {
        replay.advance(t);
        TransportEvent event;
        while (replay.poll(event)) {
            std::vector<NetInputFrame> frames;
            if (takeInput(event, frames)) replayReceiver.receive(frames, replayHostTick);
        }
        while (tickAt(t) > replayHostTick) {
            ++replayHostTick;
            if (replayHostTick >= firstTick + kInputDelayTicks + 1)
                consumeInto(replayed, replayReceiver, replayHostTick - kInputDelayTicks);
        }
        if (replayed.total >= live.total) break;
    }
    bool identical = replayed.frames.size() == live.frames.size();
    for (std::size_t i = 0; identical && i < live.frames.size(); ++i) identical = replayed.frames[i] == live.frames[i];
    check(identical, "the replay rebuilds exactly the same input stream");

    std::printf("nectar_net_soak_test: %s\n", failures == 0 ? "all tests passed" : "FAILED");
    return failures == 0 ? 0 : 1;
}
