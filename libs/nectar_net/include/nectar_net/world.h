#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace nectar::net {

// Network identity of a replicated entity, assigned only by the host:
//   scene_epoch:16 | kind:8 | generation:8 | counter:32
// The epoch changes on every day/level/cave floor load, so an id from the
// previous scene can never resolve to a new entity. It is the low 16 bits of
// the scene barrier's epoch, which only grows within a session.
enum class EntityKind : std::uint8_t {
    Captain = 1,
    Pikmin = 2,
    Enemy = 3,
    Pellet = 4,
    Item = 5,
    Obstacle = 6,
    Treasure = 7,
    Projectile = 8,
    Gate = 9,
    Other = 15,
};

struct NetEntityId {
    std::uint16_t sceneEpoch = 0;
    EntityKind kind = EntityKind::Other;
    std::uint8_t generation = 0;
    std::uint32_t counter = 0;

    std::uint64_t pack() const;
    static bool unpack(std::uint64_t value, NetEntityId& id); // false for unknown kinds or counter 0
    bool operator==(const NetEntityId& o) const { return pack() == o.pack(); }
};

bool isKnownEntityKind(std::uint8_t kind);

// One entity's state in a game-defined schema (schema 0 is reserved).
struct EntityRecord {
    std::uint64_t id = 0;
    std::uint8_t schema = 0;
    std::vector<std::uint8_t> data;
};

struct WorldDeltaLimits {
    std::size_t maxRecords = 512;
    std::size_t maxRecordBytes = 1024;
};

constexpr std::uint8_t kWorldDeltaKeyframe = 0x01; // records are complete, not changes

// Payload of WorldDelta/WorldSnapshot: flags u8 | count u16 |
// count * (id u64 | schema u8 | length u16 | data). Every record must belong
// to `expectedEpoch`; duplicates are refused.
std::vector<std::uint8_t> encodeWorldDelta(std::uint8_t flags, const std::vector<EntityRecord>& records);
bool decodeWorldDelta(const std::uint8_t* data, std::size_t size, std::uint16_t expectedEpoch,
                      const WorldDeltaLimits& limits, std::uint8_t& flags, std::vector<EntityRecord>& records);

// Splits records into payloads of at most maxPayloadBytes, each decodable on
// its own, so a lost datagram never corrupts another. A record that cannot
// fit alone is reported in `oversized` and left out.
std::vector<std::vector<std::uint8_t>> packWorldDeltas(std::uint8_t flags, const std::vector<EntityRecord>& records,
                                                       std::size_t maxPayloadBytes,
                                                       std::vector<std::uint64_t>* oversized = nullptr);

// StateHash payload: tick u64 | hash u64 | entity count u32. Exchanged at
// 2 Hz to detect silent divergence; a mismatch triggers a keyframe.
struct StateHashPayload {
    std::uint64_t tick = 0;
    std::uint64_t hash = 0;
    std::uint32_t entities = 0;
};

constexpr std::size_t kStateHashBytes = 20;
std::vector<std::uint8_t> encodeStateHash(const StateHashPayload& payload);
bool decodeStateHash(const std::uint8_t* data, std::size_t size, StateHashPayload& payload);

} // namespace nectar::net
