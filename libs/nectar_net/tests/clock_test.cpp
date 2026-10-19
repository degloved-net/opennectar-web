#include "nectar_net/clock.h"

#include <cstdio>
#include <cstdlib>

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

struct Rng {
    std::uint64_t s = 12345;
    std::uint32_t next()
    {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return static_cast<std::uint32_t>(s >> 16);
    }
};

// Host clock = guest clock + offset; the host's tick n starts at n*1000/30 ms.
struct Host {
    std::int64_t offsetMs;
    std::uint64_t tickAt(std::uint64_t guestMs, std::uint32_t& phase) const
    {
        const std::int64_t hostMs = static_cast<std::int64_t>(guestMs) + offsetMs;
        const std::int64_t tick = hostMs * kTicksPerSecond / 1000;
        phase = static_cast<std::uint32_t>(hostMs - tick * 1000 / kTicksPerSecond);
        return static_cast<std::uint64_t>(tick);
    }
};

// One ping/pong over a link with the given one-way delays.
void exchange(ClockEstimator& clock, const Host& host, std::uint64_t sendAt, std::uint32_t up, std::uint32_t down)
{
    PongPayload pong;
    pong.echoTimeMs = sendAt;
    pong.hostTick = host.tickAt(sendAt + up, pong.hostTickPhaseMs);
    clock.addSample(pong, sendAt + up + down);
}

void testCodec()
{
    PongPayload pong{7, 123456789ULL, 987654321ULL, 20};
    const auto bytes = encodePong(pong);
    PongPayload back;
    check(bytes.size() == kPongBytes && decodePong(bytes.data(), bytes.size(), back) && back.id == 7
              && back.echoTimeMs == pong.echoTimeMs && back.hostTick == pong.hostTick && back.hostTickPhaseMs == 20,
          "pong round-trips");
    auto bad = bytes;
    bad[23] = 99;
    check(!decodePong(bad.data(), bad.size(), back), "phase beyond one tick is refused");
    check(!decodePong(bytes.data(), bytes.size() - 1, back), "short pong");
    const auto ping = encodePing({3, 55});
    PingPayload p;
    check(decodePing(ping.data(), ping.size(), p) && p.id == 3 && p.senderTimeMs == 55, "ping round-trips");
}

void testSteadyLink()
{
    const Host host{987654};
    ClockEstimator clock;
    check(!clock.ready() && clock.hostTickAt(1000) == 0, "no estimate before samples");
    for (std::uint64_t t = 0; t < 10; ++t) exchange(clock, host, 10000 + t * 500, 40, 40);
    std::uint32_t phase = 0;
    std::uint32_t truePhase = 0;
    const std::uint64_t now = 20000;
    const std::uint64_t estimate = clock.hostTickAt(now, &phase);
    const std::uint64_t truth = host.tickAt(now, truePhase);
    check(estimate + 1 >= truth && estimate <= truth + 1, "steady link: tick within one of the truth");
    check(clock.rttMs() == 80 && clock.minRttMs() == 80, "RTT measured");
    check(clock.interpolationDelayMs() >= 60 && clock.interpolationDelayMs() <= 150, "buffer within bounds");
}

void testJitteryLink()
{
    const Host host{-5000}; // host clock behind the guest's
    ClockEstimator clock;
    Rng rng;
    for (std::uint64_t t = 0; t < 200; ++t) {
        const std::uint32_t up = 50 + rng.next() % 60;
        const std::uint32_t down = 50 + rng.next() % 60;
        exchange(clock, host, 100000 + t * 250, up, down);
    }
    std::uint32_t phase = 0;
    const std::uint64_t now = 160000;
    const std::uint64_t estimate = clock.hostTickAt(now);
    const std::uint64_t truth = host.tickAt(now, phase);
    const long error = static_cast<long>(estimate) - static_cast<long>(truth);
    check(std::labs(error) <= 1, "jittery link: the min-RTT sample keeps the tick within one");
    check(clock.jitterMs() >= 10, "jitter is noticed");
    check(clock.interpolationDelayMs() > 60 && clock.interpolationDelayMs() <= 150, "buffer grows with jitter");
}

void testBadSamples()
{
    ClockEstimator clock;
    PongPayload future;
    future.echoTimeMs = 5000;
    check(!clock.addSample(future, 4000), "an echo from the future is ignored");
    PongPayload ancient;
    ancient.echoTimeMs = 0;
    check(!clock.addSample(ancient, 60000), "a 60 s round trip is ignored");
    check(!clock.ready(), "ignored samples do not count");
    ClockEstimator huge;
    for (int i = 0; i < 20; ++i) {
        PongPayload pong;
        pong.echoTimeMs = 1000;
        huge.addSample(pong, 1000 + (i % 2 == 0 ? 100 : 2000)); // wildly swinging RTT
    }
    check(huge.jitterMs() > 45 && huge.interpolationDelayMs() == 150, "the buffer never exceeds 150 ms");
    ClockEstimator steady;
    for (int i = 0; i < 20; ++i) {
        PongPayload pong;
        pong.echoTimeMs = 1000;
        steady.addSample(pong, 1000 + 2000);
    }
    check(steady.interpolationDelayMs() < 100, "a slow but steady link needs little buffer");
}

} // namespace

int main()
{
    testCodec();
    testSteadyLink();
    testJitteryLink();
    testBadSamples();
    std::printf("nectar_net_clock_test: %s\n", failures == 0 ? "all tests passed" : "FAILED");
    return failures == 0 ? 0 : 1;
}
