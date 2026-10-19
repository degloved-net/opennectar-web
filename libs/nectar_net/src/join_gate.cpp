#include "nectar_net/join_gate.h"

#include "nectar_net/protocol.h"
#include "nectar_net/secure_memory.h"

#include <cstring>

namespace nectar::net {

namespace {

// ClientHello payload: session nonce (16) + join secret (16).
// HostHello payload:   session nonce (16) + verdict (1).
constexpr std::size_t kClientHelloPayloadBytes = 32;
constexpr std::size_t kHostHelloPayloadBytes   = 17;
constexpr std::uint8_t kVerdictAccepted        = 0;
constexpr std::uint8_t kVerdictRefused         = 1;

bool encodeHandshake(MessageType type, const std::uint8_t* payload, std::size_t size,
                     std::vector<std::uint8_t>& output)
{
    Message message;
    message.header.type = type;
    message.payload.assign(payload, payload + size);
    const bool ok = encodeMessage(message, output);
    secureWipe(message.payload.data(), message.payload.size());
    return ok;
}

// Decodes a quarantine message, rejecting anything but the expected type and
// exact payload size. Never allocates more than maxPayload bytes.
JoinError decodeHandshake(Delivery delivery, const std::uint8_t* data, std::size_t size, MessageType expected,
                          std::size_t payloadBytes, Message& message)
{
    if (delivery != Delivery::ReliableOrdered) return JoinError::UnexpectedMessage;
    const DecodeError decoded = decodeMessage(data, size, message, payloadBytes);
    if (decoded == DecodeError::UnsupportedMajor) return JoinError::ProtocolMismatch;
    if (decoded != DecodeError::None) return JoinError::Malformed;
    if (message.header.type != expected) return JoinError::UnexpectedMessage;
    if (message.payload.size() != payloadBytes) return JoinError::Malformed;
    return JoinError::None;
}

} // namespace

JoinCredentials joinCredentialsFromCode(const ConnectionCode& code)
{
    JoinCredentials credentials;
    credentials.sessionNonce = code.sessionNonce;
    credentials.joinSecret = code.joinSecret;
    return credentials;
}

HostJoinGate::HostJoinGate(const JoinCredentials& credentials, std::uint64_t timeoutMs)
    : mCredentials(credentials), mTimeoutMs(timeoutMs)
{
}

HostJoinGate::~HostJoinGate()
{
    secureWipe(&mCredentials, sizeof(mCredentials));
}

void HostJoinGate::onConnected(std::uint64_t nowMs)
{
    mState = JoinState::AwaitingJoin;
    mError = JoinError::None;
    mDeadlineMs = nowMs + mTimeoutMs;
}

void HostJoinGate::onDisconnected()
{
    mState = JoinState::Idle;
}

JoinAction HostJoinGate::reject(JoinError error)
{
    mState = JoinState::Rejected;
    mError = error;
    return JoinAction::Close;
}

JoinAction HostJoinGate::onMessage(Delivery delivery, const std::uint8_t* data, std::size_t size,
                                   std::vector<std::uint8_t>& output)
{
    if (mState == JoinState::Authenticated) return JoinAction::Forward;
    if (mState != JoinState::AwaitingJoin) return JoinAction::Close;

    Message message;
    const JoinError error =
        decodeHandshake(delivery, data, size, MessageType::ClientHello, kClientHelloPayloadBytes, message);
    if (error != JoinError::None) {
        secureWipe(message.payload.data(), message.payload.size());
        return reject(error);
    }

    // Evaluate both comparisons so timing does not reveal which one failed.
    const bool nonceOk =
        constantTimeEqual(message.payload.data(), mCredentials.sessionNonce.data(), mCredentials.sessionNonce.size());
    const bool secretOk = constantTimeEqual(message.payload.data() + 16, mCredentials.joinSecret.data(),
                                            mCredentials.joinSecret.size());
    secureWipe(message.payload.data(), message.payload.size());
    const bool accepted = nonceOk & secretOk;

    // A refusal carries no room nonce: a peer without the secret learns
    // nothing about the room, and a guest with a stale code can still tell
    // "refused" apart from a malformed answer.
    std::uint8_t reply[kHostHelloPayloadBytes] = {};
    if (accepted) std::memcpy(reply, mCredentials.sessionNonce.data(), 16);
    reply[16] = accepted ? kVerdictAccepted : kVerdictRefused;
    if (!encodeHandshake(MessageType::HostHello, reply, sizeof(reply), output)) return reject(JoinError::Malformed);

    if (!accepted) {
        reject(JoinError::Refused);
        return JoinAction::SendAndClose;
    }
    mState = JoinState::Authenticated;
    return JoinAction::Send;
}

JoinAction HostJoinGate::tick(std::uint64_t nowMs)
{
    if (mState == JoinState::AwaitingJoin && nowMs >= mDeadlineMs) return reject(JoinError::Timeout);
    return JoinAction::None;
}

GuestJoinGate::GuestJoinGate(const JoinCredentials& credentials, std::uint64_t timeoutMs)
    : mCredentials(credentials), mTimeoutMs(timeoutMs)
{
}

GuestJoinGate::~GuestJoinGate()
{
    secureWipe(&mCredentials, sizeof(mCredentials));
}

JoinAction GuestJoinGate::onConnected(std::uint64_t nowMs, std::vector<std::uint8_t>& output)
{
    std::uint8_t hello[kClientHelloPayloadBytes];
    std::memcpy(hello, mCredentials.sessionNonce.data(), 16);
    std::memcpy(hello + 16, mCredentials.joinSecret.data(), 16);
    const bool ok = encodeHandshake(MessageType::ClientHello, hello, sizeof(hello), output);
    secureWipe(hello, sizeof(hello));
    if (!ok) return reject(JoinError::Malformed);

    mState = JoinState::AwaitingAccept;
    mError = JoinError::None;
    mDeadlineMs = nowMs + mTimeoutMs;
    return JoinAction::Send;
}

void GuestJoinGate::onDisconnected()
{
    mState = JoinState::Idle;
}

JoinAction GuestJoinGate::reject(JoinError error)
{
    mState = JoinState::Rejected;
    mError = error;
    return JoinAction::Close;
}

JoinAction GuestJoinGate::onMessage(Delivery delivery, const std::uint8_t* data, std::size_t size)
{
    if (mState == JoinState::Authenticated) return JoinAction::Forward;
    if (mState != JoinState::AwaitingAccept) return JoinAction::Close;

    Message message;
    const JoinError error =
        decodeHandshake(delivery, data, size, MessageType::HostHello, kHostHelloPayloadBytes, message);
    if (error != JoinError::None) return reject(error);

    switch (message.payload[16]) {
    case kVerdictAccepted:
        // An acceptance must name this room.
        if (!constantTimeEqual(message.payload.data(), mCredentials.sessionNonce.data(), 16))
            return reject(JoinError::Malformed);
        mState = JoinState::Authenticated;
        return JoinAction::None;
    case kVerdictRefused:
        return reject(JoinError::Refused);
    default:
        return reject(JoinError::Malformed);
    }
}

JoinAction GuestJoinGate::tick(std::uint64_t nowMs)
{
    if (mState == JoinState::AwaitingAccept && nowMs >= mDeadlineMs) return reject(JoinError::Timeout);
    return JoinAction::None;
}

const char* joinErrorString(JoinError error)
{
    switch (error) {
    case JoinError::None:              return "none";
    case JoinError::Timeout:           return "timeout";
    case JoinError::UnexpectedMessage: return "unexpected_message";
    case JoinError::Malformed:         return "malformed";
    case JoinError::ProtocolMismatch:  return "protocol_mismatch";
    case JoinError::Refused:           return "refused";
    case JoinError::RateLimited:       return "rate_limited";
    case JoinError::Incompatible:      return "incompatible";
    }
    return "invalid_join_error";
}

} // namespace nectar::net
