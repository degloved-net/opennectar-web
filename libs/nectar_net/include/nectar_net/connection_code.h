#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace nectar::net {

// Self-contained invitation for a direct UDP session. The checksum only
// catches transcription errors; joinSecret is checked by the join quarantine
// (ADR-0002). Wire format frozen on 2026-10-14, see docs/netplay/PROTOCOL.md:
//   magic "NCJC" | version u8 | protocolMajor u16 | flags u16 | endpointCount u8
//   | expires u32 (Unix seconds) | sessionNonce[16] | joinSecret[16]
//   | endpoints (family u8, port u16, priority u8, 4 or 16 address bytes)
//   | CRC32 u32, all big endian, then grouped Crockford Base32.
struct DirectEndpoint {
    enum class Family : std::uint8_t { Ipv4 = 4, Ipv6 = 6 };

    Family family = Family::Ipv4;
    std::array<std::uint8_t, 16> address{};
    std::uint16_t port = 0;
    std::uint8_t priority = 0;
};

struct ConnectionCode {
    static constexpr std::uint8_t kFormatVersion = 1;
    static constexpr std::size_t kMaxEndpoints = 4;

    std::uint16_t protocolMajor = 0;
    std::uint16_t flags = 0;
    std::uint64_t expiresUnixSeconds = 0; // stored as u32 on the wire
    std::array<std::uint8_t, 16> sessionNonce{};
    std::array<std::uint8_t, 16> joinSecret{};
    std::vector<DirectEndpoint> endpoints;
};

enum class ConnectionCodeError {
    None,
    Empty,
    TooLong,
    BadPrefix,
    BadCharacter,
    BadPadding,
    Truncated,
    TrailingData,
    BadMagic,
    UnsupportedVersion,
    InvalidEndpointCount,
    InvalidAddressFamily,
    InvalidPort,
    InvalidProtocol,
    MissingSessionMaterial,
    Expired,
    RandomSourceFailure,
    ChecksumMismatch,
};

// Produces grouped Crockford Base32 beginning with "NECTAR1-". Returns an
// empty string when the object is invalid (for example no endpoint or port 0).
std::string encodeConnectionCode(const ConnectionCode& code);

// Strict, allocation-bounded parser. ASCII spaces and hyphens are accepted as
// visual separators; ambiguous Crockford characters O/I/L are accepted.
ConnectionCodeError decodeConnectionCode(const std::string& text, ConnectionCode& code);

// Creates the security-sensitive fields from the operating system CSPRNG.
// nowUnixSeconds is explicit so callers and tests do not depend on a clock
// hidden inside the codec.
ConnectionCodeError createConnectionCode(std::uint16_t protocolMajor, std::uint16_t flags,
                                         std::uint64_t nowUnixSeconds, std::uint64_t lifetimeSeconds,
                                         const std::vector<DirectEndpoint>& endpoints, ConnectionCode& code);

// Checks the fields whose meaning depends on the receiving session. Decoding
// alone remains useful for diagnostics and never silently consults wall time.
ConnectionCodeError validateConnectionCode(const ConnectionCode& code, std::uint16_t expectedProtocolMajor,
                                           std::uint64_t nowUnixSeconds);

const char* connectionCodeErrorString(ConnectionCodeError error);

} // namespace nectar::net
