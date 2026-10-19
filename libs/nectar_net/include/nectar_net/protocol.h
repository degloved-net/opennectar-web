#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace nectar::net {

constexpr std::uint32_t kProtocolMagic = 0x4e435452U;
constexpr std::uint16_t kProtocolMajor = 0;
constexpr std::uint16_t kProtocolMinor = 1;

enum class MessageType : std::uint8_t {
    ClientProbe          = 1,
    HostCookie          = 2,
    ClientHello         = 3,
    HostHello           = 4,
    SessionRules        = 5,
    CompatibilityResult = 6,
    Ping                 = 7,
    Pong                 = 8,
    CompatibilityOffer   = 9,
    InputBundle          = 16,
    AppliedInputs        = 17,
    AppliedRepairRequest = 18, // guest -> host: resend applied ticks [from, from + count)
    WorldDelta           = 32,
    WorldSnapshot        = 33,
    StateHash            = 34,
    ScenePrepare         = 48,
    SceneReady           = 49,
    SceneStart           = 50,
    LeaveCoopRequest     = 64,
    RejoinRequest        = 65,
    PlayerControlChanged = 66,
    ContentManifest      = 80,
    ContentDecision      = 81,
    ContentChunk         = 82,
    Disconnect           = 96,
};

struct MessageHeader {
    std::uint32_t magic          = kProtocolMagic;
    std::uint16_t protocolMajor  = kProtocolMajor;
    std::uint16_t protocolMinor  = kProtocolMinor;
    MessageType type             = MessageType::Ping;
    std::uint8_t flags           = 0;
    std::uint16_t channelEpoch   = 0;
    std::uint64_t sessionId      = 0;
    std::uint32_t sceneEpoch     = 0;
    std::uint32_t sequence       = 0;
    std::uint32_t ackSequence    = 0;
    std::uint64_t simulationTick = 0;
    std::uint16_t payloadLength  = 0;
};

struct Message {
    MessageHeader header;
    std::vector<std::uint8_t> payload;
};

enum class DecodeError {
    None,
    Truncated,
    BadMagic,
    UnsupportedMajor,
    UnknownType,
    UnknownFlags,
    PayloadTooLarge,
    LengthMismatch,
};

bool isKnownMessageType(MessageType type);
bool encodeMessage(const Message& message, std::vector<std::uint8_t>& output);
DecodeError decodeMessage(const std::uint8_t* data, std::size_t size, Message& output,
                          std::size_t maxPayloadBytes);
const char* decodeErrorName(DecodeError error);

} // namespace nectar::net
