#include "nectar_net/compatibility.h"
#include "nectar_net/protocol.h"

#include <cstdio>
#include <string>
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

CompatibilityInfo sample()
{
    CompatibilityInfo info;
    info.protocolMinor = kProtocolMinor;
    info.game = static_cast<std::uint8_t>(GameId::Pikmin1);
    info.region = 1;
    for (std::size_t i = 0; i < 32; ++i) {
        info.build[i] = static_cast<std::uint8_t>(i);
        info.content[i] = static_cast<std::uint8_t>(0x80U + i);
    }
    info.capabilities = 0x0000000fU;
    info.requiredCapabilities = 0x00000003U;
    return info;
}

bool same(const CompatibilityInfo& a, const CompatibilityInfo& b)
{
    return a.protocolMinor == b.protocolMinor && a.game == b.game && a.region == b.region && a.build == b.build
        && a.content == b.content && a.capabilities == b.capabilities
        && a.requiredCapabilities == b.requiredCapabilities;
}

void testCodec()
{
    std::vector<std::uint8_t> bytes;
    check(encodeCompatibilityInfo(sample(), bytes) && bytes.size() == kCompatibilityInfoBytes, "info encodes to 76 bytes");
    // Golden prefix: big-endian minor, game, region, then the build digest.
    check(bytes[0] == 0 && bytes[1] == kProtocolMinor && bytes[2] == 1 && bytes[3] == 1 && bytes[4] == 0
              && bytes[35] == 31 && bytes[36] == 0x80,
          "info layout is stable");
    check(bytes[68] == 0 && bytes[71] == 0x0f && bytes[75] == 0x03, "capabilities are big endian at the end");
    CompatibilityInfo decoded;
    check(decodeCompatibilityInfo(bytes.data(), bytes.size(), decoded) && same(decoded, sample()), "info round-trips");
    check(!decodeCompatibilityInfo(bytes.data(), bytes.size() - 1, decoded), "short info is refused");
    std::vector<std::uint8_t> longer = bytes;
    longer.push_back(0);
    check(!decodeCompatibilityInfo(longer.data(), longer.size(), decoded), "long info is refused");
    std::vector<std::uint8_t> badGame = bytes;
    badGame[2] = 9;
    check(!decodeCompatibilityInfo(badGame.data(), badGame.size(), decoded), "unknown game is refused");
    check(!decodeCompatibilityInfo(nullptr, kCompatibilityInfoBytes, decoded), "null input is refused");
}

void testCompare()
{
    const CompatibilityInfo host = sample();
    CompatibilityInfo guest = host;
    check(compareCompatibility(host, guest) == CompatibilityField::None, "identical builds are compatible");
    guest.protocolMinor = 7;
    check(compareCompatibility(host, guest) == CompatibilityField::None, "minor version alone does not refuse");

    guest = host;
    guest.game = static_cast<std::uint8_t>(GameId::Pikmin2);
    guest.region = 9;
    check(compareCompatibility(host, guest) == CompatibilityField::Game, "game is reported before region");
    guest = host;
    guest.region = 2;
    check(compareCompatibility(host, guest) == CompatibilityField::Region, "region mismatch");
    guest = host;
    guest.build[5] ^= 1;
    check(compareCompatibility(host, guest) == CompatibilityField::Build, "build mismatch");
    guest = host;
    guest.content[31] ^= 1;
    check(compareCompatibility(host, guest) == CompatibilityField::Content, "content mismatch");
    guest = host;
    guest.capabilities = 0x1;
    check(compareCompatibility(host, guest) == CompatibilityField::Capabilities, "guest lacks a required capability");
    guest = host;
    guest.requiredCapabilities = 0x100;
    check(compareCompatibility(host, guest) == CompatibilityField::Capabilities, "host lacks a required capability");
    check(std::string(compatibilityFieldString(CompatibilityField::Region)) == "region", "field strings are stable");
}

// Runs a full exchange; returns the guest's final state.
CompatibilityState exchange(const CompatibilityInfo& hostInfo, const CompatibilityInfo& guestInfo,
                            JoinAction& hostAction, CompatibilityCheck* guestOut = nullptr)
{
    CompatibilityCheck host(CompatibilityCheck::Role::Host, hostInfo);
    CompatibilityCheck guest(CompatibilityCheck::Role::Guest, guestInfo);
    std::vector<std::uint8_t> offer, result, unused;
    host.start(0, unused);
    guest.start(0, offer);
    hostAction = host.onMessage(Delivery::ReliableOrdered, offer.data(), offer.size(), result);
    guest.onMessage(Delivery::ReliableOrdered, result.data(), result.size(), unused);
    if (guestOut != nullptr) *guestOut = guest;
    return guest.state();
}

void testStateMachine()
{
    JoinAction hostAction = JoinAction::None;
    check(exchange(sample(), sample(), hostAction) == CompatibilityState::Compatible && hostAction == JoinAction::Send,
          "compatible pair completes");

    CompatibilityInfo pal = sample();
    pal.region = 2;
    CompatibilityCheck guest(CompatibilityCheck::Role::Guest, pal);
    check(exchange(sample(), pal, hostAction, &guest) == CompatibilityState::Incompatible
              && hostAction == JoinAction::SendAndClose,
          "incompatible pair is refused by the host");
    check(guest.mismatch() == CompatibilityField::Region && guest.peer().region == 1,
          "guest learns the field and the host's region");

    // A host that claims "compatible" for a pair its own info shows to be
    // incompatible is a protocol violation for the guest.
    CompatibilityCheck liar(CompatibilityCheck::Role::Host, sample());
    CompatibilityCheck victim(CompatibilityCheck::Role::Guest, pal);
    std::vector<std::uint8_t> offer, result, unused;
    liar.start(0, unused);
    victim.start(0, offer);
    liar.onMessage(Delivery::ReliableOrdered, offer.data(), offer.size(), result);
    // Payload byte 0 (offset 42 in the message) is the verdict.
    result[42] = static_cast<std::uint8_t>(CompatibilityField::None);
    check(victim.onMessage(Delivery::ReliableOrdered, result.data(), result.size(), unused) == JoinAction::Close
              && victim.state() == CompatibilityState::Failed,
          "guest refuses a verdict contradicted by the host's info");
}

void testViolations()
{
    std::vector<std::uint8_t> out;
    CompatibilityCheck host(CompatibilityCheck::Role::Host, sample());
    host.start(0, out);
    Message ping;
    ping.header.type = MessageType::Ping;
    std::vector<std::uint8_t> bytes;
    encodeMessage(ping, bytes);
    check(host.onMessage(Delivery::ReliableOrdered, bytes.data(), bytes.size(), out) == JoinAction::Close
              && host.state() == CompatibilityState::Failed,
          "gameplay before compatibility is a violation");

    CompatibilityCheck guest(CompatibilityCheck::Role::Guest, sample());
    std::vector<std::uint8_t> offer;
    guest.start(0, offer);
    CompatibilityCheck host2(CompatibilityCheck::Role::Host, sample());
    host2.start(0, out);
    check(host2.onMessage(Delivery::Unreliable, offer.data(), offer.size(), out) == JoinAction::Close,
          "offer on the unreliable channel is refused");

    CompatibilityCheck slow(CompatibilityCheck::Role::Host, sample(), 1000);
    slow.start(100, out);
    check(slow.tick(1099) == JoinAction::None, "waits until the deadline");
    check(slow.tick(1100) == JoinAction::Close && slow.state() == CompatibilityState::Failed, "times out");

    CompatibilityCheck done(CompatibilityCheck::Role::Guest, sample());
    JoinAction ignored;
    exchange(sample(), sample(), ignored, &done);
    check(done.onMessage(Delivery::Unreliable, bytes.data(), bytes.size(), out) == JoinAction::Forward,
          "after compatibility gameplay is forwarded");
}

} // namespace

int main()
{
    testCodec();
    testCompare();
    testStateMachine();
    testViolations();
    std::printf("nectar_net_compatibility_test: %s\n", failures == 0 ? "all tests passed" : "FAILED");
    return failures == 0 ? 0 : 1;
}
