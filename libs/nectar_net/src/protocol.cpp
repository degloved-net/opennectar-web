#include "nectar_net/protocol.h"

#include "nectar_net/byte_io.h"
#include "nectar_net/limits.h"

#include <limits>
#include <utility>

namespace nectar::net {

bool isKnownMessageType(MessageType type)
{
    switch (type) {
    case MessageType::ClientProbe:
    case MessageType::HostCookie:
    case MessageType::ClientHello:
    case MessageType::HostHello:
    case MessageType::SessionRules:
    case MessageType::CompatibilityResult:
    case MessageType::Ping:
    case MessageType::Pong:
    case MessageType::CompatibilityOffer:
    case MessageType::InputBundle:
    case MessageType::AppliedInputs:
    case MessageType::AppliedRepairRequest:
    case MessageType::WorldDelta:
    case MessageType::WorldSnapshot:
    case MessageType::StateHash:
    case MessageType::ScenePrepare:
    case MessageType::SceneReady:
    case MessageType::SceneStart:
    case MessageType::LeaveCoopRequest:
    case MessageType::RejoinRequest:
    case MessageType::PlayerControlChanged:
    case MessageType::ContentManifest:
    case MessageType::ContentDecision:
    case MessageType::ContentChunk:
    case MessageType::Disconnect:
        return true;
    }
    return false;
}

bool encodeMessage(const Message& message, std::vector<std::uint8_t>& output)
{
    if (!isKnownMessageType(message.header.type) || message.header.flags != 0 ||
        message.header.magic != kProtocolMagic ||
        message.header.protocolMajor != kProtocolMajor ||
        message.payload.size() > limits::kMaxNormalPayloadBytes ||
        message.payload.size() > std::numeric_limits<std::uint16_t>::max()) {
        return false;
    }

    ByteWriter writer(limits::kHeaderBytes + message.payload.size());
    writer.writeU32(message.header.magic);
    writer.writeU16(message.header.protocolMajor);
    writer.writeU16(message.header.protocolMinor);
    writer.writeU8(static_cast<std::uint8_t>(message.header.type));
    writer.writeU8(message.header.flags);
    writer.writeU16(message.header.channelEpoch);
    writer.writeU64(message.header.sessionId);
    writer.writeU32(message.header.sceneEpoch);
    writer.writeU32(message.header.sequence);
    writer.writeU32(message.header.ackSequence);
    writer.writeU64(message.header.simulationTick);
    writer.writeU16(static_cast<std::uint16_t>(message.payload.size()));
    writer.writeBytes(message.payload.data(), message.payload.size());
    if (!writer.ok()) return false;
    output = writer.bytes();
    return true;
}

DecodeError decodeMessage(const std::uint8_t* data, std::size_t size, Message& output,
                          std::size_t maxPayloadBytes)
{
    if (data == nullptr || size < limits::kHeaderBytes) return DecodeError::Truncated;

    ByteReader reader(data, size);
    Message decoded;
    std::uint8_t type = 0;
    if (!reader.readU32(decoded.header.magic) ||
        !reader.readU16(decoded.header.protocolMajor) ||
        !reader.readU16(decoded.header.protocolMinor) || !reader.readU8(type) ||
        !reader.readU8(decoded.header.flags) || !reader.readU16(decoded.header.channelEpoch) ||
        !reader.readU64(decoded.header.sessionId) || !reader.readU32(decoded.header.sceneEpoch) ||
        !reader.readU32(decoded.header.sequence) || !reader.readU32(decoded.header.ackSequence) ||
        !reader.readU64(decoded.header.simulationTick) ||
        !reader.readU16(decoded.header.payloadLength)) {
        return DecodeError::Truncated;
    }
    decoded.header.type = static_cast<MessageType>(type);

    if (decoded.header.magic != kProtocolMagic) return DecodeError::BadMagic;
    if (decoded.header.protocolMajor != kProtocolMajor) return DecodeError::UnsupportedMajor;
    if (!isKnownMessageType(decoded.header.type)) return DecodeError::UnknownType;
    if (decoded.header.flags != 0) return DecodeError::UnknownFlags;
    if (decoded.header.payloadLength > maxPayloadBytes) return DecodeError::PayloadTooLarge;
    if (reader.remaining() != decoded.header.payloadLength) return DecodeError::LengthMismatch;

    decoded.payload.resize(decoded.header.payloadLength);
    if (!reader.readBytes(decoded.payload.data(), decoded.payload.size())) return DecodeError::Truncated;
    output = std::move(decoded);
    return DecodeError::None;
}

const char* decodeErrorName(DecodeError error)
{
    switch (error) {
    case DecodeError::None:             return "none";
    case DecodeError::Truncated:        return "truncated";
    case DecodeError::BadMagic:         return "bad_magic";
    case DecodeError::UnsupportedMajor: return "unsupported_major";
    case DecodeError::UnknownType:      return "unknown_type";
    case DecodeError::UnknownFlags:     return "unknown_flags";
    case DecodeError::PayloadTooLarge:  return "payload_too_large";
    case DecodeError::LengthMismatch:   return "length_mismatch";
    }
    return "invalid_decode_error";
}

} // namespace nectar::net
