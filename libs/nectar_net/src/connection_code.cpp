#include "nectar_net/connection_code.h"

#include "nectar_net/byte_io.h"
#include "nectar_net/secure_random.h"

#include <algorithm>
#include <cctype>

namespace nectar::net {
namespace {

constexpr char kPrefix[] = "NECTAR1-";
constexpr char kAlphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
constexpr std::size_t kMaxTextBytes = 384;
constexpr std::size_t kMaxWireBytes = 192;
constexpr std::uint8_t kMagic[4] = {'N', 'C', 'J', 'C'};

std::uint32_t crc32(const std::uint8_t* data, std::size_t size)
{
    std::uint32_t crc = 0xffffffffU;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xedb88320U & mask);
        }
    }
    return ~crc;
}

int base32Value(char input)
{
    const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(input)));
    if (c == 'O') return 0;
    if (c == 'I' || c == 'L') return 1;
    const char* found = std::find(kAlphabet, kAlphabet + 32, c);
    return found == kAlphabet + 32 ? -1 : static_cast<int>(found - kAlphabet);
}

std::string base32Encode(const std::vector<std::uint8_t>& bytes)
{
    std::string output;
    output.reserve((bytes.size() * 8U + 4U) / 5U);
    std::uint32_t accumulator = 0;
    unsigned bits = 0;
    for (std::uint8_t byte : bytes) {
        accumulator = (accumulator << 8U) | byte;
        bits += 8;
        while (bits >= 5) {
            bits -= 5;
            output.push_back(kAlphabet[(accumulator >> bits) & 31U]);
        }
    }
    if (bits != 0) output.push_back(kAlphabet[(accumulator << (5U - bits)) & 31U]);
    return output;
}

ConnectionCodeError base32Decode(const std::string& text, std::vector<std::uint8_t>& bytes)
{
    bytes.clear();
    bytes.reserve((text.size() * 5U) / 8U);
    std::uint32_t accumulator = 0;
    unsigned bits = 0;
    for (char c : text) {
        if (c == '-' || std::isspace(static_cast<unsigned char>(c))) continue;
        const int value = base32Value(c);
        if (value < 0) return ConnectionCodeError::BadCharacter;
        accumulator = (accumulator << 5U) | static_cast<std::uint32_t>(value);
        bits += 5;
        if (bits >= 8) {
            bits -= 8;
            if (bytes.size() >= kMaxWireBytes) return ConnectionCodeError::TooLong;
            bytes.push_back(static_cast<std::uint8_t>(accumulator >> bits));
        }
    }
    // Canonical form only: a whole spare character (5+ leftover bits) or
    // non-zero padding would let two different texts decode to one code.
    if (bits >= 5) return ConnectionCodeError::BadPadding;
    if (bits != 0 && (accumulator & ((1U << bits) - 1U)) != 0) return ConnectionCodeError::BadPadding;
    return ConnectionCodeError::None;
}

bool validEndpoint(const DirectEndpoint& endpoint)
{
    return (endpoint.family == DirectEndpoint::Family::Ipv4 || endpoint.family == DirectEndpoint::Family::Ipv6)
        && endpoint.port != 0;
}

template <std::size_t Size>
bool allZero(const std::array<std::uint8_t, Size>& value)
{
    return std::all_of(value.begin(), value.end(), [](std::uint8_t byte) { return byte == 0; });
}

} // namespace

std::string encodeConnectionCode(const ConnectionCode& code)
{
    if (code.protocolMajor == 0) return {};
    if (allZero(code.sessionNonce) || allZero(code.joinSecret)) return {};
    if (code.endpoints.empty() || code.endpoints.size() > ConnectionCode::kMaxEndpoints) return {};
    if (code.expiresUnixSeconds > UINT32_MAX) return {};
    for (const DirectEndpoint& endpoint : code.endpoints)
        if (!validEndpoint(endpoint)) return {};

    ByteWriter writer(kMaxWireBytes);
    writer.writeBytes(kMagic, sizeof(kMagic));
    writer.writeU8(ConnectionCode::kFormatVersion);
    writer.writeU16(code.protocolMajor);
    writer.writeU16(code.flags);
    writer.writeU8(static_cast<std::uint8_t>(code.endpoints.size()));
    writer.writeU32(static_cast<std::uint32_t>(code.expiresUnixSeconds));
    writer.writeBytes(code.sessionNonce.data(), code.sessionNonce.size());
    writer.writeBytes(code.joinSecret.data(), code.joinSecret.size());
    for (const DirectEndpoint& endpoint : code.endpoints) {
        writer.writeU8(static_cast<std::uint8_t>(endpoint.family));
        writer.writeU16(endpoint.port);
        writer.writeU8(endpoint.priority);
        const std::size_t addressBytes = endpoint.family == DirectEndpoint::Family::Ipv4 ? 4U : 16U;
        writer.writeBytes(endpoint.address.data(), addressBytes);
    }
    if (!writer.ok()) return {};

    std::vector<std::uint8_t> wire = writer.bytes();
    const std::uint32_t checksum = crc32(wire.data(), wire.size());
    ByteWriter completed(kMaxWireBytes);
    completed.writeBytes(wire.data(), wire.size());
    completed.writeU32(checksum);
    if (!completed.ok()) return {};

    const std::string compact = base32Encode(completed.bytes());
    std::string result = kPrefix;
    for (std::size_t i = 0; i < compact.size(); ++i) {
        if (i != 0 && i % 5U == 0) result.push_back('-');
        result.push_back(compact[i]);
    }
    return result;
}

ConnectionCodeError decodeConnectionCode(const std::string& text, ConnectionCode& code)
{
    code = ConnectionCode{};
    if (text.empty()) return ConnectionCodeError::Empty;
    if (text.size() > kMaxTextBytes) return ConnectionCodeError::TooLong;

    std::string normalized;
    normalized.reserve(text.size());
    for (char c : text)
        if (!std::isspace(static_cast<unsigned char>(c))) normalized.push_back(c);
    if (normalized.size() < sizeof(kPrefix) - 1U) return ConnectionCodeError::BadPrefix;
    for (std::size_t i = 0; i < sizeof(kPrefix) - 1U; ++i) {
        const char got = static_cast<char>(std::toupper(static_cast<unsigned char>(normalized[i])));
        if (got != kPrefix[i]) return ConnectionCodeError::BadPrefix;
    }

    std::vector<std::uint8_t> wire;
    const ConnectionCodeError base32Error = base32Decode(normalized.substr(sizeof(kPrefix) - 1U), wire);
    if (base32Error != ConnectionCodeError::None) return base32Error;
    constexpr std::size_t kFixedBytes = 4U + 1U + 2U + 2U + 1U + 4U + 16U + 16U;
    if (wire.size() < kFixedBytes + 8U + 4U) return ConnectionCodeError::Truncated;

    const std::size_t bodySize = wire.size() - 4U;
    const std::uint32_t expected = crc32(wire.data(), bodySize);
    ByteReader checksumReader(wire.data() + bodySize, 4U);
    std::uint32_t actual = 0;
    if (!checksumReader.readU32(actual) || actual != expected) return ConnectionCodeError::ChecksumMismatch;

    ByteReader reader(wire.data(), bodySize);
    std::uint8_t magic[4]{};
    std::uint8_t version = 0;
    std::uint8_t endpointCount = 0;
    std::uint32_t expires = 0;
    if (!reader.readBytes(magic, sizeof(magic)) || !std::equal(magic, magic + 4, kMagic))
        return ConnectionCodeError::BadMagic;
    if (!reader.readU8(version)) return ConnectionCodeError::Truncated;
    if (version != ConnectionCode::kFormatVersion) return ConnectionCodeError::UnsupportedVersion;
    if (!reader.readU16(code.protocolMajor) || !reader.readU16(code.flags) || !reader.readU8(endpointCount)
        || !reader.readU32(expires)
        || !reader.readBytes(code.sessionNonce.data(), code.sessionNonce.size())
        || !reader.readBytes(code.joinSecret.data(), code.joinSecret.size()))
        return ConnectionCodeError::Truncated;
    code.expiresUnixSeconds = expires;
    if (endpointCount == 0 || endpointCount > ConnectionCode::kMaxEndpoints)
        return ConnectionCodeError::InvalidEndpointCount;
    if (code.protocolMajor == 0) return ConnectionCodeError::InvalidProtocol;
    if (allZero(code.sessionNonce) || allZero(code.joinSecret)) return ConnectionCodeError::MissingSessionMaterial;

    code.endpoints.reserve(endpointCount);
    for (std::uint8_t i = 0; i < endpointCount; ++i) {
        DirectEndpoint endpoint;
        std::uint8_t family = 0;
        if (!reader.readU8(family) || !reader.readU16(endpoint.port) || !reader.readU8(endpoint.priority))
            return ConnectionCodeError::Truncated;
        if (family == static_cast<std::uint8_t>(DirectEndpoint::Family::Ipv4))
            endpoint.family = DirectEndpoint::Family::Ipv4;
        else if (family == static_cast<std::uint8_t>(DirectEndpoint::Family::Ipv6))
            endpoint.family = DirectEndpoint::Family::Ipv6;
        else
            return ConnectionCodeError::InvalidAddressFamily;
        if (endpoint.port == 0) return ConnectionCodeError::InvalidPort;
        const std::size_t addressBytes = endpoint.family == DirectEndpoint::Family::Ipv4 ? 4U : 16U;
        if (!reader.readBytes(endpoint.address.data(), addressBytes)) return ConnectionCodeError::Truncated;
        code.endpoints.push_back(endpoint);
    }
    return reader.remaining() == 0 ? ConnectionCodeError::None : ConnectionCodeError::TrailingData;
}

ConnectionCodeError createConnectionCode(std::uint16_t protocolMajor, std::uint16_t flags,
                                         std::uint64_t nowUnixSeconds, std::uint64_t lifetimeSeconds,
                                         const std::vector<DirectEndpoint>& endpoints, ConnectionCode& code)
{
    code = ConnectionCode{};
    if (protocolMajor == 0) return ConnectionCodeError::InvalidProtocol;
    if (endpoints.empty() || endpoints.size() > ConnectionCode::kMaxEndpoints)
        return ConnectionCodeError::InvalidEndpointCount;
    for (const DirectEndpoint& endpoint : endpoints) {
        if (endpoint.family != DirectEndpoint::Family::Ipv4 && endpoint.family != DirectEndpoint::Family::Ipv6)
            return ConnectionCodeError::InvalidAddressFamily;
        if (endpoint.port == 0) return ConnectionCodeError::InvalidPort;
    }
    if (lifetimeSeconds == 0 || nowUnixSeconds > UINT32_MAX || lifetimeSeconds > UINT32_MAX - nowUnixSeconds)
        return ConnectionCodeError::Expired;

    code.protocolMajor = protocolMajor;
    code.flags = flags;
    code.expiresUnixSeconds = nowUnixSeconds + lifetimeSeconds;
    code.endpoints = endpoints;
    if (!secureRandomBytes(code.sessionNonce.data(), code.sessionNonce.size())
        || !secureRandomBytes(code.joinSecret.data(), code.joinSecret.size())) {
        code = ConnectionCode{};
        return ConnectionCodeError::RandomSourceFailure;
    }
    if (allZero(code.sessionNonce) || allZero(code.joinSecret)) {
        code = ConnectionCode{};
        return ConnectionCodeError::RandomSourceFailure;
    }
    return ConnectionCodeError::None;
}

ConnectionCodeError validateConnectionCode(const ConnectionCode& code, std::uint16_t expectedProtocolMajor,
                                           std::uint64_t nowUnixSeconds)
{
    if (code.protocolMajor == 0 || code.protocolMajor != expectedProtocolMajor)
        return ConnectionCodeError::InvalidProtocol;
    if (allZero(code.sessionNonce) || allZero(code.joinSecret))
        return ConnectionCodeError::MissingSessionMaterial;
    if (code.expiresUnixSeconds == 0 || nowUnixSeconds > code.expiresUnixSeconds)
        return ConnectionCodeError::Expired;
    if (code.endpoints.empty() || code.endpoints.size() > ConnectionCode::kMaxEndpoints)
        return ConnectionCodeError::InvalidEndpointCount;
    for (const DirectEndpoint& endpoint : code.endpoints) {
        if (endpoint.family != DirectEndpoint::Family::Ipv4 && endpoint.family != DirectEndpoint::Family::Ipv6)
            return ConnectionCodeError::InvalidAddressFamily;
        if (endpoint.port == 0) return ConnectionCodeError::InvalidPort;
    }
    return ConnectionCodeError::None;
}

const char* connectionCodeErrorString(ConnectionCodeError error)
{
    switch (error) {
    case ConnectionCodeError::None: return "none";
    case ConnectionCodeError::Empty: return "empty code";
    case ConnectionCodeError::TooLong: return "code is too long";
    case ConnectionCodeError::BadPrefix: return "invalid code prefix";
    case ConnectionCodeError::BadCharacter: return "invalid Base32 character";
    case ConnectionCodeError::BadPadding: return "invalid Base32 padding";
    case ConnectionCodeError::Truncated: return "truncated code";
    case ConnectionCodeError::TrailingData: return "unexpected trailing data";
    case ConnectionCodeError::BadMagic: return "invalid code magic";
    case ConnectionCodeError::UnsupportedVersion: return "unsupported code version";
    case ConnectionCodeError::InvalidEndpointCount: return "invalid endpoint count";
    case ConnectionCodeError::InvalidAddressFamily: return "invalid address family";
    case ConnectionCodeError::InvalidPort: return "invalid port";
    case ConnectionCodeError::InvalidProtocol: return "invalid or incompatible protocol";
    case ConnectionCodeError::MissingSessionMaterial: return "missing session security material";
    case ConnectionCodeError::Expired: return "connection code has expired";
    case ConnectionCodeError::RandomSourceFailure: return "secure random source failed";
    case ConnectionCodeError::ChecksumMismatch: return "checksum mismatch";
    }
    return "unknown error";
}

} // namespace nectar::net
