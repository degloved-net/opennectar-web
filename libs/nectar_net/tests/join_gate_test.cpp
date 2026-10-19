#include "nectar_net/join_gate.h"
#include "nectar_net/protocol.h"
#include "nectar_net/secure_memory.h"

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

JoinCredentials credentials(std::uint8_t seed)
{
    JoinCredentials value;
    for (std::size_t i = 0; i < value.sessionNonce.size(); ++i) value.sessionNonce[i] = static_cast<std::uint8_t>(seed + i);
    for (std::size_t i = 0; i < value.joinSecret.size(); ++i)
        value.joinSecret[i] = static_cast<std::uint8_t>(seed + 0x40U + i);
    return value;
}

std::vector<std::uint8_t> guestHello(const JoinCredentials& creds)
{
    GuestJoinGate guest(creds);
    std::vector<std::uint8_t> hello;
    guest.onConnected(0, hello);
    return hello;
}

std::vector<std::uint8_t> encoded(MessageType type, std::size_t payloadBytes)
{
    Message message;
    message.header.type = type;
    message.payload.assign(payloadBytes, 0x5a);
    std::vector<std::uint8_t> output;
    encodeMessage(message, output);
    return output;
}

void testHappyPath()
{
    const JoinCredentials creds = credentials(1);
    HostJoinGate host(creds);
    GuestJoinGate guest(creds);
    host.onConnected(100);

    std::vector<std::uint8_t> hello;
    check(guest.onConnected(100, hello) == JoinAction::Send, "guest sends its hello on connect");
    check(guest.state() == JoinState::AwaitingAccept, "guest waits for the verdict");

    std::vector<std::uint8_t> reply;
    check(host.onMessage(Delivery::ReliableOrdered, hello.data(), hello.size(), reply) == JoinAction::Send,
          "host accepts the right secret");
    check(host.state() == JoinState::Authenticated, "host authenticated");
    check(reply.size() <= hello.size(), "host reply is never larger than the request");

    check(guest.onMessage(Delivery::ReliableOrdered, reply.data(), reply.size()) == JoinAction::None,
          "guest consumes the acceptance");
    check(guest.state() == JoinState::Authenticated, "guest authenticated");

    const std::vector<std::uint8_t> ping = encoded(MessageType::Ping, 0);
    std::vector<std::uint8_t> unused;
    check(host.onMessage(Delivery::Unreliable, ping.data(), ping.size(), unused) == JoinAction::Forward,
          "authenticated host forwards gameplay");
    check(guest.onMessage(Delivery::Unreliable, ping.data(), ping.size()) == JoinAction::Forward,
          "authenticated guest forwards gameplay");
    check(host.tick(1000000) == JoinAction::None, "no timeout once authenticated");
}

void testRefused(const JoinCredentials& hostCreds, const JoinCredentials& guestCreds, const char* label)
{
    HostJoinGate host(hostCreds);
    GuestJoinGate guest(guestCreds);
    host.onConnected(0);
    std::vector<std::uint8_t> hello;
    guest.onConnected(0, hello);
    std::vector<std::uint8_t> reply;
    check(host.onMessage(Delivery::ReliableOrdered, hello.data(), hello.size(), reply) == JoinAction::SendAndClose,
          label);
    check(host.state() == JoinState::Rejected && host.error() == JoinError::Refused, "host records refusal");
    check(reply.size() <= hello.size(), "refusal is never larger than the request");
    bool nonceHidden = true;
    for (std::size_t i = reply.size() - 17; i < reply.size() - 1; ++i) nonceHidden = nonceHidden && reply[i] == 0;
    check(nonceHidden, "a refusal does not reveal the room nonce");

    const std::vector<std::uint8_t> ping = encoded(MessageType::Ping, 0);
    std::vector<std::uint8_t> unused;
    check(host.onMessage(Delivery::ReliableOrdered, ping.data(), ping.size(), unused) == JoinAction::Close,
          "rejected host never forwards");
    check(host.onMessage(Delivery::ReliableOrdered, hello.data(), hello.size(), unused) == JoinAction::Close,
          "rejected connection gets no second attempt");
}

void testWrongSecretAndSession()
{
    const JoinCredentials good = credentials(1);
    JoinCredentials badSecret = good;
    badSecret.joinSecret[15] ^= 1;
    testRefused(good, badSecret, "host refuses a wrong secret");

    JoinCredentials staleRoom = good;
    staleRoom.sessionNonce[0] ^= 1;
    testRefused(good, staleRoom, "host refuses a code from another room");

    // The guest must report a refusal it receives.
    HostJoinGate host(good);
    GuestJoinGate guest(badSecret);
    host.onConnected(0);
    std::vector<std::uint8_t> hello, reply;
    guest.onConnected(0, hello);
    host.onMessage(Delivery::ReliableOrdered, hello.data(), hello.size(), reply);
    check(guest.onMessage(Delivery::ReliableOrdered, reply.data(), reply.size()) == JoinAction::Close
              && guest.error() == JoinError::Refused,
          "guest reports refusal");
}

void expectHostReject(const std::vector<std::uint8_t>& bytes, Delivery delivery, JoinError error, const char* label)
{
    HostJoinGate host(credentials(1));
    host.onConnected(0);
    std::vector<std::uint8_t> reply;
    check(host.onMessage(delivery, bytes.data(), bytes.size(), reply) == JoinAction::Close && reply.empty()
              && host.state() == JoinState::Rejected && host.error() == error,
          label);
}

void testQuarantine()
{
    const std::vector<std::uint8_t> hello = guestHello(credentials(1));

    expectHostReject(encoded(MessageType::Ping, 0), Delivery::ReliableOrdered, JoinError::UnexpectedMessage,
                     "gameplay before the secret closes without reply");
    expectHostReject(encoded(MessageType::InputBundle, 8), Delivery::ReliableOrdered, JoinError::UnexpectedMessage,
                     "input before the secret closes without reply");
    expectHostReject(hello, Delivery::Unreliable, JoinError::UnexpectedMessage,
                     "secret on the unreliable channel is refused");
    expectHostReject(encoded(MessageType::ClientHello, 31), Delivery::ReliableOrdered, JoinError::Malformed,
                     "short hello is malformed");
    expectHostReject(encoded(MessageType::ClientHello, 4096), Delivery::ReliableOrdered, JoinError::Malformed,
                     "oversized hello is malformed");
    expectHostReject(std::vector<std::uint8_t>(hello.begin(), hello.end() - 1), Delivery::ReliableOrdered,
                     JoinError::Malformed, "truncated hello is malformed");
    expectHostReject(std::vector<std::uint8_t>(3, 0xff), Delivery::ReliableOrdered, JoinError::Malformed,
                     "garbage is malformed");

    std::vector<std::uint8_t> otherMajor = hello;
    otherMajor[5] ^= 1; // low byte of protocolMajor
    expectHostReject(otherMajor, Delivery::ReliableOrdered, JoinError::ProtocolMismatch,
                     "other protocol major is reported as mismatch");

    HostJoinGate idle(credentials(1));
    std::vector<std::uint8_t> reply;
    check(idle.onMessage(Delivery::ReliableOrdered, hello.data(), hello.size(), reply) == JoinAction::Close,
          "host without a connection ignores data");
}

void testTimeoutsAndRetry()
{
    const JoinCredentials creds = credentials(9);
    HostJoinGate host(creds, 5000);
    host.onConnected(1000);
    check(host.tick(5999) == JoinAction::None, "host waits until the deadline");
    check(host.tick(6000) == JoinAction::Close && host.error() == JoinError::Timeout, "host times out a silent peer");

    host.onDisconnected();
    host.onConnected(7000);
    check(host.state() == JoinState::AwaitingJoin && host.error() == JoinError::None, "new connection starts clean");
    const std::vector<std::uint8_t> hello = guestHello(creds);
    std::vector<std::uint8_t> reply;
    check(host.onMessage(Delivery::ReliableOrdered, hello.data(), hello.size(), reply) == JoinAction::Send,
          "reconnecting guest can authenticate after a timeout");

    GuestJoinGate guest(creds, 5000);
    std::vector<std::uint8_t> out;
    guest.onConnected(0, out);
    check(guest.tick(4999) == JoinAction::None, "guest waits until the deadline");
    check(guest.tick(5000) == JoinAction::Close && guest.error() == JoinError::Timeout, "guest times out");
}

void testGuestValidation()
{
    const JoinCredentials creds = credentials(3);
    auto fresh = [&](GuestJoinGate& guest) {
        std::vector<std::uint8_t> out;
        guest.onConnected(0, out);
    };

    GuestJoinGate early(creds);
    const std::vector<std::uint8_t> ping = encoded(MessageType::Ping, 0);
    check(early.onMessage(Delivery::ReliableOrdered, ping.data(), ping.size()) == JoinAction::Close,
          "guest ignores data before connecting");

    GuestJoinGate wrongType(creds);
    fresh(wrongType);
    check(wrongType.onMessage(Delivery::ReliableOrdered, ping.data(), ping.size()) == JoinAction::Close
              && wrongType.error() == JoinError::UnexpectedMessage,
          "guest refuses gameplay before acceptance");

    // An acceptance for another room must not authenticate the guest.
    HostJoinGate otherHost(credentials(4));
    otherHost.onConnected(0);
    const std::vector<std::uint8_t> otherHello = guestHello(credentials(4));
    std::vector<std::uint8_t> otherReply;
    otherHost.onMessage(Delivery::ReliableOrdered, otherHello.data(), otherHello.size(), otherReply);
    GuestJoinGate wrongRoom(creds);
    fresh(wrongRoom);
    check(wrongRoom.onMessage(Delivery::ReliableOrdered, otherReply.data(), otherReply.size()) == JoinAction::Close
              && wrongRoom.state() == JoinState::Rejected,
          "guest refuses an acceptance for another room");

    GuestJoinGate badVerdict(creds);
    fresh(badVerdict);
    HostJoinGate host(creds);
    host.onConnected(0);
    const std::vector<std::uint8_t> hello = guestHello(creds);
    std::vector<std::uint8_t> reply;
    host.onMessage(Delivery::ReliableOrdered, hello.data(), hello.size(), reply);
    reply.back() = 7;
    check(badVerdict.onMessage(Delivery::ReliableOrdered, reply.data(), reply.size()) == JoinAction::Close
              && badVerdict.error() == JoinError::Malformed,
          "guest refuses an unknown verdict");
}

void testSecureMemory()
{
    const std::uint8_t a[4] = {1, 2, 3, 4};
    const std::uint8_t b[4] = {1, 2, 3, 5};
    check(constantTimeEqual(a, a, sizeof(a)), "equal buffers compare equal");
    check(!constantTimeEqual(a, b, sizeof(a)), "different buffers compare different");
    check(!constantTimeEqual(a, nullptr, sizeof(a)), "null buffer never compares equal");
    std::uint8_t secret[8] = {9, 9, 9, 9, 9, 9, 9, 9};
    secureWipe(secret, sizeof(secret));
    bool zero = true;
    for (std::uint8_t byte : secret) zero = zero && byte == 0;
    check(zero, "secureWipe zeroes the buffer");
    check(std::string(joinErrorString(JoinError::Refused)) == "refused", "error strings are stable");
}

} // namespace

int main()
{
    testHappyPath();
    testWrongSecretAndSession();
    testQuarantine();
    testTimeoutsAndRetry();
    testGuestValidation();
    testSecureMemory();

    std::printf("nectar_net_join_gate_test: %s\n", failures == 0 ? "all tests passed" : "FAILED");
    return failures == 0 ? 0 : 1;
}
