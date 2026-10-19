#include "nectar_net/connection_code.h"

#include <cstdio>

namespace {

int failures = 0;

void check(bool condition, const char* message)
{
    if (!condition) {
        std::printf("FAIL: %s\n", message);
        ++failures;
    }
}

nectar::net::ConnectionCode sample()
{
    nectar::net::ConnectionCode code;
    code.protocolMajor = 2;
    code.flags = 3;
    code.expiresUnixSeconds = 0x01020304U;
    for (std::size_t i = 0; i < code.sessionNonce.size(); ++i) code.sessionNonce[i] = static_cast<std::uint8_t>(i);
    for (std::size_t i = 0; i < code.joinSecret.size(); ++i) code.joinSecret[i] = static_cast<std::uint8_t>(0x20U + i);
    nectar::net::DirectEndpoint ipv4;
    ipv4.family = nectar::net::DirectEndpoint::Family::Ipv4;
    ipv4.address[0] = 192;
    ipv4.address[1] = 0;
    ipv4.address[2] = 2;
    ipv4.address[3] = 10;
    ipv4.port = 48810;
    ipv4.priority = 100;
    code.endpoints.push_back(ipv4);
    nectar::net::DirectEndpoint ipv6;
    ipv6.family = nectar::net::DirectEndpoint::Family::Ipv6;
    ipv6.address[0] = 0x20;
    ipv6.address[1] = 0x01;
    ipv6.address[15] = 1;
    ipv6.port = 48810;
    ipv6.priority = 80;
    code.endpoints.push_back(ipv6);
    return code;
}

} // namespace

int main()
{
    using namespace nectar::net;
    const ConnectionCode source = sample();
    const std::string encoded = encodeConnectionCode(source);
    check(encoded.rfind("NECTAR1-", 0) == 0, "code has the versioned human prefix");
    check(encoded.size() < 384, "code stays inside the parser cap");

    ConnectionCode decoded;
    check(decodeConnectionCode(encoded, decoded) == ConnectionCodeError::None, "code roundtrips");
    check(decoded.protocolMajor == source.protocolMajor && decoded.flags == source.flags, "metadata roundtrips");
    check(decoded.sessionNonce == source.sessionNonce && decoded.joinSecret == source.joinSecret,
          "session material roundtrips");
    check(decoded.endpoints.size() == 2 && decoded.endpoints[0].port == 48810
              && decoded.endpoints[1].family == DirectEndpoint::Family::Ipv6,
          "IPv4 and IPv6 endpoints roundtrip");
    check(validateConnectionCode(decoded, 2, 0x01020303U) == ConnectionCodeError::None,
          "fresh code validates for the expected protocol");
    check(validateConnectionCode(decoded, 3, 0x01020303U) == ConnectionCodeError::InvalidProtocol,
          "different protocol is rejected");
    check(validateConnectionCode(decoded, 2, 0x01020305U) == ConnectionCodeError::Expired,
          "expired code is rejected");

    // Golden vectors (format frozen 2026-10-14): any change here breaks codes
    // already shared between released builds.
    ConnectionCode golden;
    golden.protocolMajor = 1;
    golden.expiresUnixSeconds = 1790000000U;
    for (std::size_t i = 0; i < 16; ++i) {
        golden.sessionNonce[i] = static_cast<std::uint8_t>(0x10U + i);
        golden.joinSecret[i] = static_cast<std::uint8_t>(0xa0U + i);
    }
    DirectEndpoint goldenV4;
    goldenV4.family = DirectEndpoint::Family::Ipv4;
    goldenV4.address[0] = 203;
    goldenV4.address[2] = 113;
    goldenV4.address[3] = 7;
    goldenV4.port = 48810;
    golden.endpoints.push_back(goldenV4);
    const std::string goldenV4Text = "NECTAR1-9S1MM-GR100-0G000-1DARK-Q00G2-49165-0N2RB-HG68T-3CE1T-7GZM2-GT58X-4MPKA-FA59N-ANTSB-DENW2-"
                                    "BXAG0-SC072-1VZ4B-WYP";
    check(encodeConnectionCode(golden) == goldenV4Text, "IPv4 golden vector is stable");
    DirectEndpoint goldenV6;
    goldenV6.family = DirectEndpoint::Family::Ipv6;
    goldenV6.address[0] = 0x20;
    goldenV6.address[1] = 0x01;
    goldenV6.address[2] = 0x0d;
    goldenV6.address[3] = 0xb8;
    goldenV6.address[15] = 1;
    goldenV6.port = 48810;
    goldenV6.priority = 1;
    golden.endpoints.insert(golden.endpoints.begin(), goldenV6);
    const std::string goldenDualText = "NECTAR1-9S1MM-GR100-0G000-2DARK-Q00G2-49165-0N2RB-HG68T-3CE1T-7GZM2-GT58X-4MPKA-FA59N-ANTSB-DENW3-"
                                      "BXAG1-400GV-E0000-00000-00000-00000-42BXA-G0SC0-721SS-N2PF2";
    check(encodeConnectionCode(golden) == goldenDualText, "IPv6+IPv4 golden vector is stable");
    check(decodeConnectionCode(goldenDualText, decoded) == ConnectionCodeError::None
              && decoded.expiresUnixSeconds == 1790000000U && decoded.joinSecret == golden.joinSecret
              && decoded.endpoints.size() == 2 && decoded.endpoints[1].address[0] == 203,
          "golden vector decodes");
    check(goldenV4Text.size() <= 120, "a single IPv4 code stays around 120 characters");

    ConnectionCode farFuture = source;
    farFuture.expiresUnixSeconds = 0x100000000ULL;
    check(encodeConnectionCode(farFuture).empty(), "expiry beyond 32 bits cannot be encoded");

    std::string compact;
    for (char c : encoded)
        if (c != '-') compact.push_back(c);
    check(decodeConnectionCode(compact, decoded) == ConnectionCodeError::BadPrefix,
          "the visible version prefix is mandatory");

    std::string corrupt = encoded;
    corrupt.back() = corrupt.back() == '0' ? '1' : '0';
    check(decodeConnectionCode(corrupt, decoded) == ConnectionCodeError::ChecksumMismatch,
          "single-character corruption is rejected");

    corrupt = encoded + "0";
    check(decodeConnectionCode(corrupt, decoded) != ConnectionCodeError::None, "trailing Base32 data is rejected");
    for (int extra = 1; extra <= 8; ++extra) {
        check(decodeConnectionCode(goldenV4Text + std::string(static_cast<std::size_t>(extra), '0'), decoded)
                  != ConnectionCodeError::None,
              "any number of trailing zero characters is rejected");
    }
    check(decodeConnectionCode("NPIX2-old", decoded) == ConnectionCodeError::BadPrefix,
          "legacy two-way ICE offers cannot be mistaken for direct codes");
    check(decodeConnectionCode(std::string(385, 'A'), decoded) == ConnectionCodeError::TooLong,
          "oversized input is rejected before decoding");

    ConnectionCode invalid = source;
    invalid.endpoints[0].port = 0;
    check(encodeConnectionCode(invalid).empty(), "port zero cannot be encoded");
    invalid = source;
    invalid.endpoints.clear();
    check(encodeConnectionCode(invalid).empty(), "an invitation requires a reachable endpoint");
    invalid = source;
    invalid.joinSecret.fill(0);
    check(encodeConnectionCode(invalid).empty(), "an all-zero join secret cannot be encoded");

    ConnectionCode generated;
    check(createConnectionCode(2, 1, 1000, 300, source.endpoints, generated)
              == ConnectionCodeError::None,
          "host invitation is created from the operating system CSPRNG");
    check(generated.expiresUnixSeconds == 1300 && generated.protocolMajor == 2,
          "generated invitation carries protocol and expiration");
    check(generated.sessionNonce != ConnectionCode{}.sessionNonce
              && generated.joinSecret != ConnectionCode{}.joinSecret,
          "generated invitation has non-zero security material");
    ConnectionCode generatedAgain;
    check(createConnectionCode(2, 1, 1000, 300, source.endpoints, generatedAgain)
              == ConnectionCodeError::None
              && generatedAgain.sessionNonce != generated.sessionNonce
              && generatedAgain.joinSecret != generated.joinSecret,
          "separate invitations receive separate security material");

    std::printf("nectar_net_connection_code_test: %s\n", failures == 0 ? "all tests passed" : "FAILED");
    return failures == 0 ? 0 : 1;
}
