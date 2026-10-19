#include "nectar_net/world.h"

#include "nectar_net/byte_io.h"

#include <algorithm>
#include <unordered_set>

namespace nectar::net {

namespace {

constexpr std::size_t kDeltaHeaderBytes = 3;
constexpr std::size_t kRecordHeaderBytes = 11;
constexpr std::size_t kMaxRecordsPerPayload = 0xffff;

} // namespace

std::uint64_t NetEntityId::pack() const
{
    return (static_cast<std::uint64_t>(sceneEpoch) << 48) | (static_cast<std::uint64_t>(kind) << 40)
         | (static_cast<std::uint64_t>(generation) << 32) | counter;
}

bool isKnownEntityKind(std::uint8_t kind)
{
    return (kind >= static_cast<std::uint8_t>(EntityKind::Captain) && kind <= static_cast<std::uint8_t>(EntityKind::Gate))
        || kind == static_cast<std::uint8_t>(EntityKind::Other);
}

bool NetEntityId::unpack(std::uint64_t value, NetEntityId& id)
{
    const auto kind = static_cast<std::uint8_t>(value >> 40);
    const auto counter = static_cast<std::uint32_t>(value);
    if (!isKnownEntityKind(kind) || counter == 0) return false;
    id.sceneEpoch = static_cast<std::uint16_t>(value >> 48);
    id.kind = static_cast<EntityKind>(kind);
    id.generation = static_cast<std::uint8_t>(value >> 32);
    id.counter = counter;
    return true;
}

std::vector<std::uint8_t> encodeWorldDelta(std::uint8_t flags, const std::vector<EntityRecord>& records)
{
    if (records.size() > kMaxRecordsPerPayload || (flags & ~kWorldDeltaKeyframe) != 0) return {};
    std::size_t size = kDeltaHeaderBytes;
    for (const EntityRecord& r : records) {
        if (r.schema == 0 || r.data.size() > 0xffff) return {};
        size += kRecordHeaderBytes + r.data.size();
    }
    ByteWriter writer(size);
    writer.writeU8(flags);
    writer.writeU16(static_cast<std::uint16_t>(records.size()));
    for (const EntityRecord& r : records) {
        writer.writeU64(r.id);
        writer.writeU8(r.schema);
        writer.writeU16(static_cast<std::uint16_t>(r.data.size()));
        writer.writeBytes(r.data.data(), r.data.size());
    }
    return writer.ok() ? writer.bytes() : std::vector<std::uint8_t>{};
}

bool decodeWorldDelta(const std::uint8_t* data, std::size_t size, std::uint16_t expectedEpoch,
                      const WorldDeltaLimits& limits, std::uint8_t& flags, std::vector<EntityRecord>& records)
{
    if (data == nullptr || size < kDeltaHeaderBytes) return false;
    ByteReader reader(data, size);
    std::uint8_t decodedFlags = 0;
    std::uint16_t count = 0;
    if (!reader.readU8(decodedFlags) || !reader.readU16(count)) return false;
    if ((decodedFlags & ~kWorldDeltaKeyframe) != 0 || count > limits.maxRecords) return false;
    // Cheap bound before reserving anything: every record needs its header.
    if (reader.remaining() < static_cast<std::size_t>(count) * kRecordHeaderBytes) return false;
    std::vector<EntityRecord> decoded(count);
    std::unordered_set<std::uint64_t> seen;
    seen.reserve(count);
    for (EntityRecord& r : decoded) {
        std::uint16_t length = 0;
        if (!reader.readU64(r.id) || !reader.readU8(r.schema) || !reader.readU16(length)) return false;
        NetEntityId id;
        if (r.schema == 0 || length > limits.maxRecordBytes || !NetEntityId::unpack(r.id, id)
            || id.sceneEpoch != expectedEpoch || !seen.insert(r.id).second) {
            return false;
        }
        if (reader.remaining() < length) return false;
        r.data.resize(length);
        if (!reader.readBytes(r.data.data(), length)) return false;
    }
    if (reader.remaining() != 0) return false;
    flags = decodedFlags;
    records = std::move(decoded);
    return true;
}

std::vector<std::vector<std::uint8_t>> packWorldDeltas(std::uint8_t flags, const std::vector<EntityRecord>& records,
                                                       std::size_t maxPayloadBytes,
                                                       std::vector<std::uint64_t>* oversized)
{
    std::vector<std::vector<std::uint8_t>> payloads;
    std::vector<EntityRecord> batch;
    std::size_t batchBytes = kDeltaHeaderBytes;
    auto flush = [&]() {
        if (batch.empty()) return;
        payloads.push_back(encodeWorldDelta(flags, batch));
        batch.clear();
        batchBytes = kDeltaHeaderBytes;
    };
    for (const EntityRecord& r : records) {
        const std::size_t bytes = kRecordHeaderBytes + r.data.size();
        if (kDeltaHeaderBytes + bytes > maxPayloadBytes) {
            if (oversized != nullptr) oversized->push_back(r.id);
            continue;
        }
        if (batchBytes + bytes > maxPayloadBytes || batch.size() == kMaxRecordsPerPayload) flush();
        batch.push_back(r);
        batchBytes += bytes;
    }
    flush();
    return payloads;
}

std::vector<std::uint8_t> encodeStateHash(const StateHashPayload& payload)
{
    ByteWriter writer(kStateHashBytes);
    writer.writeU64(payload.tick);
    writer.writeU64(payload.hash);
    writer.writeU32(payload.entities);
    return writer.ok() ? writer.bytes() : std::vector<std::uint8_t>{};
}

bool decodeStateHash(const std::uint8_t* data, std::size_t size, StateHashPayload& payload)
{
    if (data == nullptr || size != kStateHashBytes) return false;
    ByteReader reader(data, size);
    return reader.readU64(payload.tick) && reader.readU64(payload.hash) && reader.readU32(payload.entities);
}

} // namespace nectar::net
