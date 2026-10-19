#include "nectar_net/byte_io.h"
#include "nectar_net/limits.h"
#include "nectar_net/protocol.h"

#include <cstdio>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* message)
{
    if (!condition) {
        std::printf("FAIL: %s\n", message);
        ++failures;
    }
}

nectar::net::Message sampleMessage()
{
    nectar::net::Message message;
    message.header.type           = nectar::net::MessageType::Ping;
    message.header.channelEpoch   = 3;
    message.header.sessionId      = 0x0102030405060708ULL;
    message.header.sceneEpoch     = 7;
    message.header.sequence       = 11;
    message.header.ackSequence    = 9;
    message.header.simulationTick = 0x1112131415161718ULL;
    message.payload               = {0xde, 0xad, 0xbe, 0xef};
    return message;
}

} // namespace

int main()
{
    using namespace nectar::net;

    Message message = sampleMessage();
    std::vector<std::uint8_t> encoded;
    check(encodeMessage(message, encoded), "sample message encodes");
    check(encoded.size() == limits::kHeaderBytes + message.payload.size(), "wire size is exact");
    check(encoded[0] == 'N' && encoded[1] == 'C' && encoded[2] == 'T' && encoded[3] == 'R',
          "magic is big-endian NCTR");
    const std::vector<std::uint8_t> golden = {
        0x4e, 0x43, 0x54, 0x52, 0x00, 0x00, 0x00, 0x01, 0x07, 0x00, 0x00, 0x03,
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x00, 0x00, 0x00, 0x07,
        0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x09, 0x11, 0x12, 0x13, 0x14,
        0x15, 0x16, 0x17, 0x18, 0x00, 0x04, 0xde, 0xad, 0xbe, 0xef,
    };
    check(encoded == golden, "wire encoding matches the cross-platform golden vector");

    Message decoded;
    check(decodeMessage(encoded.data(), encoded.size(), decoded, 64) == DecodeError::None,
          "sample message decodes");
    check(decoded.header.sessionId == message.header.sessionId, "u64 session ID roundtrips");
    check(decoded.header.simulationTick == message.header.simulationTick, "u64 tick roundtrips");
    check(decoded.payload == message.payload, "payload roundtrips");

    for (std::size_t size = 0; size < limits::kHeaderBytes; ++size) {
        Message ignored;
        check(decodeMessage(encoded.data(), size, ignored, 64) == DecodeError::Truncated,
              "every truncated header is rejected");
    }

    std::vector<std::uint8_t> corrupt = encoded;
    corrupt[0] = 0;
    check(decodeMessage(corrupt.data(), corrupt.size(), decoded, 64) == DecodeError::BadMagic,
          "bad magic is rejected");

    corrupt = encoded;
    corrupt[4] = 0;
    corrupt[5] = 1;
    check(decodeMessage(corrupt.data(), corrupt.size(), decoded, 64) == DecodeError::UnsupportedMajor,
          "unknown major version is rejected");

    corrupt = encoded;
    corrupt[8] = 0xff;
    check(decodeMessage(corrupt.data(), corrupt.size(), decoded, 64) == DecodeError::UnknownType,
          "unknown message type is rejected");

    corrupt = encoded;
    corrupt[9] = 1;
    check(decodeMessage(corrupt.data(), corrupt.size(), decoded, 64) == DecodeError::UnknownFlags,
          "unknown flags are rejected");

    check(decodeMessage(encoded.data(), encoded.size(), decoded, 3) == DecodeError::PayloadTooLarge,
          "caller payload budget is enforced before allocation");

    corrupt = encoded;
    corrupt.push_back(0);
    check(decodeMessage(corrupt.data(), corrupt.size(), decoded, 64) == DecodeError::LengthMismatch,
          "trailing bytes are rejected");

    ByteWriter limited(2);
    check(!limited.writeU32(1) && !limited.ok(), "writer refuses to exceed its cap");
    check(!limited.writeBytes(nullptr, 0), "failed writer remains failed on empty write");

    Message empty = sampleMessage();
    empty.payload.clear();
    check(encodeMessage(empty, encoded), "empty payload encodes without dereferencing null");
    check(decodeMessage(encoded.data(), encoded.size(), decoded, 0) == DecodeError::None,
          "empty payload decodes with a zero-byte budget");

    std::printf("nectar_net_protocol_test: %s\n", failures == 0 ? "all tests passed" : "FAILED");
    return failures == 0 ? 0 : 1;
}
