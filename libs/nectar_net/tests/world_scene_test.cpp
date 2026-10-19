#include "nectar_net/scene.h"
#include "nectar_net/world.h"

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

std::uint64_t id(std::uint16_t epoch, EntityKind kind, std::uint32_t counter, std::uint8_t generation = 0)
{
    NetEntityId e;
    e.sceneEpoch = epoch;
    e.kind = kind;
    e.generation = generation;
    e.counter = counter;
    return e.pack();
}

EntityRecord record(std::uint64_t entity, std::size_t bytes, std::uint8_t schema = 1)
{
    EntityRecord r;
    r.id = entity;
    r.schema = schema;
    r.data.assign(bytes, static_cast<std::uint8_t>(bytes));
    return r;
}

void testIds()
{
    const std::uint64_t packed = id(0x1234, EntityKind::Pikmin, 0xdeadbeef, 7);
    check(packed == 0x12340207deadbeefULL, "id layout: epoch | kind | generation | counter");
    NetEntityId back;
    check(NetEntityId::unpack(packed, back) && back.sceneEpoch == 0x1234 && back.kind == EntityKind::Pikmin
              && back.generation == 7 && back.counter == 0xdeadbeef,
          "id round-trips");
    check(!NetEntityId::unpack(0x1234000700000001ULL, back), "kind 0 is refused");
    check(!NetEntityId::unpack(0x12340c0700000001ULL, back), "unknown kind 12 is refused");
    check(!NetEntityId::unpack(id(1, EntityKind::Enemy, 0), back), "counter 0 is refused");
    check(NetEntityId::unpack(id(1, EntityKind::Other, 1), back), "Other is a known kind");
}

void testDeltaCodec()
{
    std::vector<EntityRecord> records = {record(id(3, EntityKind::Captain, 1), 20),
                                         record(id(3, EntityKind::Pikmin, 2), 0, 2),
                                         record(id(3, EntityKind::Enemy, 9, 1), 100, 3)};
    const std::vector<std::uint8_t> bytes = encodeWorldDelta(kWorldDeltaKeyframe, records);
    std::uint8_t flags = 0;
    std::vector<EntityRecord> decoded;
    check(decodeWorldDelta(bytes.data(), bytes.size(), 3, WorldDeltaLimits{}, flags, decoded)
              && flags == kWorldDeltaKeyframe && decoded.size() == 3 && decoded[2].data == records[2].data
              && decoded[1].schema == 2,
          "delta round-trips");
    check(!decodeWorldDelta(bytes.data(), bytes.size(), 4, WorldDeltaLimits{}, flags, decoded),
          "records from another scene are refused");
    std::vector<EntityRecord> twice = {records[0], records[0]};
    std::vector<std::uint8_t> dup = encodeWorldDelta(0, twice);
    check(!decodeWorldDelta(dup.data(), dup.size(), 3, WorldDeltaLimits{}, flags, decoded), "duplicate ids");
    WorldDeltaLimits tight;
    tight.maxRecordBytes = 50;
    check(!decodeWorldDelta(bytes.data(), bytes.size(), 3, tight, flags, decoded), "record over the limit");
    tight = WorldDeltaLimits{};
    tight.maxRecords = 2;
    check(!decodeWorldDelta(bytes.data(), bytes.size(), 3, tight, flags, decoded), "too many records");
    std::vector<std::uint8_t> cut(bytes.begin(), bytes.end() - 1);
    check(!decodeWorldDelta(cut.data(), cut.size(), 3, WorldDeltaLimits{}, flags, decoded), "truncated delta");
    std::vector<std::uint8_t> extra = bytes;
    extra.push_back(0);
    check(!decodeWorldDelta(extra.data(), extra.size(), 3, WorldDeltaLimits{}, flags, decoded), "trailing bytes");
    std::vector<std::uint8_t> lying = {0, 0xff, 0xff}; // claims 65535 records with no bytes
    check(!decodeWorldDelta(lying.data(), lying.size(), 3, WorldDeltaLimits{}, flags, decoded),
          "an impossible count is refused before allocating");
    std::vector<std::uint8_t> badFlags = bytes;
    badFlags[0] = 0x80;
    check(!decodeWorldDelta(badFlags.data(), badFlags.size(), 3, WorldDeltaLimits{}, flags, decoded),
          "unknown flags");
    check(encodeWorldDelta(0, {record(id(3, EntityKind::Item, 5), 1, 0)}).empty(), "schema 0 is reserved");
}

void testPacking()
{
    std::vector<EntityRecord> records;
    for (std::uint32_t i = 1; i <= 200; ++i) records.push_back(record(id(5, EntityKind::Pikmin, i), 40));
    records.push_back(record(id(5, EntityKind::Enemy, 999), 2000)); // never fits in a datagram
    std::vector<std::uint64_t> oversized;
    const auto payloads = packWorldDeltas(0, records, 1100, &oversized);
    std::size_t total = 0;
    bool allFit = true;
    for (const auto& payload : payloads) {
        allFit = allFit && payload.size() <= 1100;
        std::uint8_t flags = 0;
        std::vector<EntityRecord> decoded;
        if (decodeWorldDelta(payload.data(), payload.size(), 5, WorldDeltaLimits{}, flags, decoded))
            total += decoded.size();
    }
    check(allFit && payloads.size() > 1, "records are split into datagrams that fit");
    check(total == 200, "every fitting record is in exactly one self-contained payload");
    check(oversized.size() == 1 && oversized[0] == id(5, EntityKind::Enemy, 999), "the oversized record is reported");
}

void testStateHash()
{
    const StateHashPayload hash{1234, 0x0123456789abcdefULL, 77};
    const auto bytes = encodeStateHash(hash);
    StateHashPayload back;
    check(bytes.size() == kStateHashBytes && decodeStateHash(bytes.data(), bytes.size(), back) && back.tick == 1234
              && back.hash == hash.hash && back.entities == 77,
          "state hash round-trips");
    check(!decodeStateHash(bytes.data(), bytes.size() - 1, back), "short state hash");
}

void testSceneBarrier()
{
    HostSceneBarrier host(1000);
    GuestSceneBarrier guest;
    const ScenePrepare prepare = host.prepare(42, 0xfeed, 0);
    const auto wire = encodeScenePrepare(prepare);
    ScenePrepare received;
    check(decodeScenePrepare(wire.data(), wire.size(), received) && guest.onPrepare(received), "guest starts loading");
    check(host.state() == BarrierState::Preparing && guest.state() == BarrierState::Preparing, "both preparing");
    SceneReady ready;
    check(guest.loaded(ready) && host.onReady(ready) && host.state() == BarrierState::Ready, "ready reaches the host");
    const SceneStart start = host.start(900);
    check(guest.onStart(start) && guest.state() == BarrierState::Running && guest.startTick() == 900,
          "both start at the same tick");

    check(!guest.onPrepare(received), "a repeated prepare is ignored");
    const ScenePrepare next = host.prepare(43, 1, 2000);
    check(!host.onReady(ready), "a ready for the previous scene is ignored");
    check(!guest.onStart(start), "a start for the previous scene is ignored");
    check(guest.onPrepare(next) && guest.current().sceneId == 43, "the next scene loads");
    check(!guest.onStart(SceneStart{next.epoch, 5}), "no start before the guest is ready");
    check(!guest.onPrepare(prepare), "an older prepare cannot rewind the guest");
    check(host.tick(2999) == BarrierState::Preparing && host.tick(3000) == BarrierState::TimedOut,
          "a guest that never loads times out");

    std::vector<std::uint8_t> zero = encodeScenePrepare(ScenePrepare{0, 1, 2});
    check(!decodeScenePrepare(zero.data(), zero.size(), received), "epoch 0 is refused");
    const auto readyWire = encodeSceneReady(ready);
    SceneReady readyBack;
    check(decodeSceneReady(readyWire.data(), readyWire.size(), readyBack) && readyBack.sceneId == 42,
          "ready round-trips");
    const auto startWire = encodeSceneStart(start);
    SceneStart startBack;
    check(decodeSceneStart(startWire.data(), startWire.size(), startBack) && startBack.startTick == 900,
          "start round-trips");
    std::vector<std::uint8_t> longer = startWire;
    longer.push_back(0);
    check(!decodeSceneStart(longer.data(), longer.size(), startBack), "start size is exact");
}

} // namespace

int main()
{
    testIds();
    testDeltaCodec();
    testPacking();
    testStateHash();
    testSceneBarrier();
    std::printf("nectar_net_world_scene_test: %s\n", failures == 0 ? "all tests passed" : "FAILED");
    return failures == 0 ? 0 : 1;
}
