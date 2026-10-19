#include "nectar_net/input.h"

#include <cstdio>
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
constexpr std::uint16_t kB = 0x0200;
constexpr std::uint8_t kSwarm = 0x08;
constexpr std::uint8_t kBomb = 0x01;

NetInputFrame frame(std::uint64_t tick, std::uint16_t held, std::uint8_t flags = 0)
{
    NetInputFrame f;
    f.tick = tick;
    f.held = held;
    f.stickX = 100;
    f.flags = flags;
    return f;
}

// newest-first bundle ending at `tick` with `count` frames.
std::vector<NetInputFrame> bundle(const std::vector<NetInputFrame>& history, std::uint64_t tick, std::size_t count)
{
    std::vector<NetInputFrame> out;
    for (std::size_t i = 0; i < count && tick >= i; ++i) out.push_back(history[tick - i]);
    return out;
}

void testCodec()
{
    std::vector<NetInputFrame> frames;
    for (int i = 0; i < 6; ++i) {
        NetInputFrame f = frame(1000 - static_cast<std::uint64_t>(i), static_cast<std::uint16_t>(kA | i));
        f.stickY = -127;
        f.cstickX = 5;
        f.triggerR = 200;
        f.cursorDx = -1234;
        f.cursorDy = 77;
        f.flags = kSwarm;
        frames.push_back(f);
    }
    const std::vector<std::uint8_t> bytes = encodeInputBundle(frames);
    check(bytes.size() == kInputBundleHeaderBytes + 6 * kInputFrameBytes, "bundle size: 9 + 17 per frame");
    std::vector<NetInputFrame> decoded;
    check(decodeInputBundle(bytes.data(), bytes.size(), InputLimits{}, decoded) && decoded == frames,
          "bundle round-trips");

    std::vector<NetInputFrame> gap = frames;
    gap[2].tick = 1;
    check(encodeInputBundle(gap).empty(), "non-consecutive ticks cannot be encoded");
    check(encodeInputBundle({}).empty(), "empty bundle cannot be encoded");
    std::vector<NetInputFrame> seven = frames;
    seven.push_back(frame(994, 0));
    check(encodeInputBundle(seven).empty(), "more than 6 frames cannot be encoded");

    InputLimits strict;
    strict.buttonMask = kA | 0x00ff;
    strict.flagMask = kSwarm;
    check(decodeInputBundle(bytes.data(), bytes.size(), strict, decoded), "allowed bits pass");
    std::vector<NetInputFrame> withB = frames;
    withB[0].pressed = kB;
    std::vector<std::uint8_t> bad = encodeInputBundle(withB);
    check(!decodeInputBundle(bad.data(), bad.size(), strict, decoded), "unknown button bits are refused");
    withB = frames;
    withB[1].flags = kBomb;
    bad = encodeInputBundle(withB);
    check(!decodeInputBundle(bad.data(), bad.size(), strict, decoded), "unknown flag bits are refused");
    withB = frames;
    withB[0].stickX = -128;
    bad = encodeInputBundle(withB);
    check(!decodeInputBundle(bad.data(), bad.size(), InputLimits{}, decoded), "stick -128 is refused");

    std::vector<std::uint8_t> trimmed(bytes.begin(), bytes.end() - 1);
    check(!decodeInputBundle(trimmed.data(), trimmed.size(), InputLimits{}, decoded), "truncated bundle");
    std::vector<std::uint8_t> zero = bytes;
    zero[8] = 0;
    check(!decodeInputBundle(zero.data(), zero.size(), InputLimits{}, decoded), "zero frames");
    std::vector<std::uint8_t> early = encodeInputBundle({frame(2, 0), frame(1, 0), frame(0, 0)});
    early[7] = 1; // newest tick 1 with 3 frames would underflow
    check(!decodeInputBundle(early.data(), early.size(), InputLimits{}, decoded), "ticks below zero are refused");
}

void testLossIsRepaired()
{
    // Guest presses A on tick 5 and releases on tick 8. Every even bundle is
    // lost; the host simulates two ticks behind (its input delay).
    std::vector<NetInputFrame> history;
    for (std::uint64_t t = 0; t < 24; ++t) history.push_back(frame(t, t >= 5 && t < 8 ? kA : 0));
    history[5].pressed = kA;
    history[8].released = kA;
    InputReceiver receiver;
    receiver.receive(bundle(history, 0, 1), 0);
    int presses = 0, releases = 0;
    bool allLive = true;
    for (std::uint64_t t = 1; t < 24; ++t) {
        if (t % 2 == 1) receiver.receive(bundle(history, t, 6), t);
        if (t < 2) continue;
        InputHealth health;
        const NetInputFrame f = receiver.consume(t - 2, health);
        allLive = allLive && health == InputHealth::Live && f.held == history[t - 2].held;
        presses += (f.pressed & kA) ? 1 : 0;
        releases += (f.released & kA) ? 1 : 0;
    }
    check(allLive, "with half the bundles lost, every tick still arrives live");
    check(presses == 1 && releases == 1, "exactly one press and one release");
}

void testHoldThenNeutral()
{
    InputPolicy policy;
    policy.continuousFlags = kSwarm;
    InputReceiver receiver(policy);
    NetInputFrame last = frame(10, kA, kSwarm | kBomb);
    last.pressed = kA;
    last.cursorDx = 300;
    receiver.receive({last}, 10);
    InputHealth health;
    NetInputFrame f = receiver.consume(10, health);
    check(health == InputHealth::Live && (f.pressed & kA) && f.cursorDx == 300, "live frame");
    for (std::uint64_t t = 11; t <= 16; ++t) {
        f = receiver.consume(t, health);
        check(health == InputHealth::Repeated && f.held == kA && f.stickX == 100, "held state repeats across a gap");
        check(f.pressed == 0 && f.cursorDx == 0 && (f.flags & kBomb) == 0, "one-shot events never repeat");
        check((f.flags & kSwarm) != 0, "continuous flags repeat");
    }
    f = receiver.consume(17, health);
    check(health == InputHealth::Neutral && f.held == 0 && f.stickX == 0 && (f.released & kA), "neutral after the hold");
    f = receiver.consume(18, health);
    check(health == InputHealth::Neutral && f.released == 0, "release is reported once");
    NetInputFrame back = frame(19, kA);
    receiver.receive({back}, 19);
    f = receiver.consume(19, health);
    check(health == InputHealth::Live && (f.pressed & kA), "a returning held button counts as a new press");
}

void testWindow()
{
    InputReceiver receiver;
    receiver.receive({frame(100, 0)}, 100);
    InputHealth health;
    receiver.consume(100, health);
    receiver.consume(101, health);
    check(receiver.receive({frame(101, kA), frame(100, kA)}, 102) == 0 && receiver.ignoredFrames() == 2,
          "frames for consumed ticks are ignored");
    check(receiver.receive({frame(102 + 31, kA)}, 102) == 0, "frames too far in the future are ignored");
    check(receiver.receive({frame(110, kA)}, 102) == 1, "near-future frames are kept");
    check(receiver.receive({frame(110, kB)}, 102) == 0, "a second copy of a tick is ignored");
    receiver.consume(109, health);
    const NetInputFrame f = receiver.consume(110, health);
    check(health == InputHealth::Live && f.held == kA, "first copy wins");
}

void testAppliedInputs()
{
    std::vector<std::vector<std::uint8_t>> blobs;
    for (int i = 0; i < 8; ++i) blobs.push_back(std::vector<std::uint8_t>(32, static_cast<std::uint8_t>(i)));
    const auto bytes = encodeAppliedInputs(500, blobs);
    check(bytes.size() == 10 + 8 * 32, "applied inputs: 10-byte header + 8 blobs");
    std::uint64_t tick = 0;
    std::vector<std::vector<std::uint8_t>> back;
    check(decodeAppliedInputs(bytes.data(), bytes.size(), 32, tick, back) && tick == 500 && back == blobs,
          "applied inputs round-trip");
    check(!decodeAppliedInputs(bytes.data(), bytes.size(), 16, tick, back), "blob size must match the game's");
    check(!decodeAppliedInputs(bytes.data(), bytes.size() - 1, 32, tick, back), "truncated applied inputs");
    check(encodeAppliedInputs(3, blobs).empty(), "ticks below zero cannot be encoded");
    blobs.push_back(blobs[0]);
    check(encodeAppliedInputs(500, blobs).empty(), "at most 8 ticks");
    std::vector<std::vector<std::uint8_t>> uneven = {std::vector<std::uint8_t>(32), std::vector<std::uint8_t>(31)};
    check(encodeAppliedInputs(500, uneven).empty(), "blobs must share one size");
}

} // namespace

int main()
{
    testCodec();
    testLossIsRepaired();
    testHoldThenNeutral();
    testWindow();
    testAppliedInputs();
    std::printf("nectar_net_input_test: %s\n", failures == 0 ? "all tests passed" : "FAILED");
    return failures == 0 ? 0 : 1;
}
