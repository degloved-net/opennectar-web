#include "nectar_net/replication.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

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

bool near(float a, float b, float eps = 0.01f) { return std::fabs(a - b) <= eps; }

void testCodec()
{
    CaptainState s;
    s.position = {123.5f, -4.25f, 9876.0f};
    s.velocity = {10.0f, 0.0f, -300.0f};
    s.faceDirection = 1.5f;
    s.player = 1;
    const auto bytes = encodeCaptainState(s);
    CaptainState back;
    check(bytes.size() == kCaptainStateBytes && decodeCaptainState(bytes.data(), bytes.size(), back)
              && back.position.z == 9876.0f && back.velocity.z == -300.0f && back.faceDirection == 1.5f
              && back.player == 1,
          "captain state round-trips exactly");
    auto poison = [&](std::size_t offset, float value) {
        std::vector<std::uint8_t> bad = bytes;
        std::uint32_t bits = 0;
        std::memcpy(&bits, &value, 4);
        for (int i = 0; i < 4; ++i) bad[offset + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(bits >> (24 - 8 * i));
        CaptainState out;
        return decodeCaptainState(bad.data(), bad.size(), out);
    };
    check(!poison(0, std::numeric_limits<float>::quiet_NaN()), "NaN position is refused");
    check(!poison(4, std::numeric_limits<float>::infinity()), "Inf position is refused");
    check(!poison(8, 2.0e5f), "a position outside any map is refused");
    check(!poison(12, 5.0e4f), "an absurd speed is refused");
    std::vector<std::uint8_t> badPlayer = bytes;
    badPlayer[28] = 2;
    CaptainState out;
    check(!decodeCaptainState(badPlayer.data(), badPlayer.size(), out), "unknown player is refused");
    check(!decodeCaptainState(bytes.data(), bytes.size() - 1, out), "short state is refused");
    const EntityRecord record = captainRecord(7, s);
    NetEntityId id;
    check(NetEntityId::unpack(record.id, id) && id.kind == EntityKind::Captain && id.counter == 2 && id.sceneEpoch == 7
              && record.schema == kCaptainSchema,
          "captain record id: kind Captain, counter = player + 1");
}

void testLead()
{
    check(inputLeadTicks(0, 0) == 1, "LAN: one tick of lead");
    check(inputLeadTicks(150, 30) == 5, "RTT 150, jitter 30: 135 ms -> 5 ticks");
    check(inputLeadTicks(2000, 500) == 8, "lead is capped at 8 ticks");
}

void testCorrection()
{
    PredictionCorrector corrector;
    // Prediction drifts 3 units on x per tick from tick 10 on.
    for (std::uint64_t t = 0; t < 20; ++t)
        corrector.recordPredicted(t, Vec3{static_cast<float>(t) * 10.0f + (t >= 10 ? 3.0f * (t - 9) : 0.0f), 0, 0});
    // Host says tick 12 was at x = 120 (prediction said 129).
    check(near(corrector.reconcile(12, Vec3{120.0f, 0, 0}), 9.0f), "error measured against the prediction");
    float applied = 0.0f;
    bool snapped = false;
    for (int i = 0; i < 6; ++i) applied += corrector.step(snapped).x;
    check(near(applied, -9.0f) && !snapped, "the error is spread over a few ticks");
    // A second report for a later tick must not re-apply the same error.
    check(near(corrector.reconcile(13, Vec3{130.0f, 0, 0}), 3.0f), "only the new drift is corrected next");
    check(corrector.reconcile(500, Vec3{}) == 0.0f, "unknown ticks are ignored");
    // Tick 14 predicted 155, already shifted by -9 and -3 to 143.
    check(near(corrector.reconcile(14, Vec3{140.0f + 0.1f, 0, 0}), 2.9f), "each report corrects only what is new");

    PredictionCorrector teleport;
    teleport.recordPredicted(5, Vec3{0, 0, 0});
    teleport.recordPredicted(6, Vec3{0, 0, 0});
    teleport.reconcile(5, Vec3{500.0f, 0, 0});
    Vec3 jump = teleport.step(snapped);
    check(snapped && near(jump.x, 500.0f) && teleport.snaps() == 1, "an impossible error snaps at once");
    check(near(teleport.reconcile(6, Vec3{500.0f, 0, 0}), 0.0f), "after the snap the next tick agrees");

    PredictionCorrector quiet;
    quiet.recordPredicted(1, Vec3{1.0f, 2.0f, 3.0f});
    quiet.reconcile(1, Vec3{1.1f, 2.0f, 3.0f});
    check(quiet.corrections() == 0, "float noise is ignored");
}

void testInterpolation()
{
    SnapshotInterpolator interp(3);
    CaptainState out;
    check(!interp.sample(1000, out), "nothing to show yet");
    CaptainState a, b;
    a.position = {0, 0, 0};
    a.faceDirection = 3.0f;
    b.position = {30.0f, 0, 0};
    b.velocity = {900.0f, 0, 0}; // 30 units per tick
    b.faceDirection = -3.0f;
    interp.push(10, a);
    interp.push(11, b);
    interp.push(11, a); // repeated tick ignored
    interp.push(9, b);  // older tick ignored
    check(interp.sample(10500, out) && near(out.position.x, 15.0f), "midpoint is interpolated");
    check(std::fabs(out.faceDirection) > 3.0f, "facing wraps the short way round");
    check(interp.sample(12000, out) && near(out.position.x, 60.0f), "one tick of extrapolation from velocity");
    check(interp.sample(30000, out) && near(out.position.x, 120.0f), "extrapolation stops after three ticks");
    check(interp.sample(5000, out) && near(out.position.x, 0.0f), "before the first state, hold the first");
    for (std::uint64_t t = 12; t < 100; ++t) interp.push(t, b);
    check(interp.sample(99000, out) && near(out.position.x, 30.0f), "the buffer stays bounded and current");
}

} // namespace

int main()
{
    testCodec();
    testLead();
    testCorrection();
    testInterpolation();
    std::printf("nectar_net_replication_test: %s\n", failures == 0 ? "all tests passed" : "FAILED");
    return failures == 0 ? 0 : 1;
}
