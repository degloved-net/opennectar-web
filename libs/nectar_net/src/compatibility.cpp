#include "nectar_net/compatibility.h"

#include "nectar_net/byte_io.h"
#include "nectar_net/protocol.h"

namespace nectar::net {

namespace {

// CompatibilityResult payload: field (1) + the host's info.
constexpr std::size_t kResultBytes = 1 + kCompatibilityInfoBytes;

bool encodeControl(MessageType type, const std::vector<std::uint8_t>& payload, std::vector<std::uint8_t>& output)
{
    Message message;
    message.header.type = type;
    message.payload = payload;
    return encodeMessage(message, output);
}

bool decodeControl(Delivery delivery, const std::uint8_t* data, std::size_t size, MessageType expected,
                   std::size_t payloadBytes, Message& message)
{
    return delivery == Delivery::ReliableOrdered && decodeMessage(data, size, message, payloadBytes) == DecodeError::None
        && message.header.type == expected && message.payload.size() == payloadBytes;
}

} // namespace

bool encodeCompatibilityInfo(const CompatibilityInfo& info, std::vector<std::uint8_t>& output)
{
    ByteWriter writer(kCompatibilityInfoBytes);
    writer.writeU16(info.protocolMinor);
    writer.writeU8(info.game);
    writer.writeU8(info.region);
    writer.writeBytes(info.build.data(), info.build.size());
    writer.writeBytes(info.content.data(), info.content.size());
    writer.writeU32(info.capabilities);
    writer.writeU32(info.requiredCapabilities);
    if (!writer.ok() || writer.bytes().size() != kCompatibilityInfoBytes) return false;
    output = writer.bytes();
    return true;
}

bool decodeCompatibilityInfo(const std::uint8_t* data, std::size_t size, CompatibilityInfo& info)
{
    if (data == nullptr || size != kCompatibilityInfoBytes) return false;
    ByteReader reader(data, size);
    CompatibilityInfo decoded;
    if (!reader.readU16(decoded.protocolMinor) || !reader.readU8(decoded.game) || !reader.readU8(decoded.region)
        || !reader.readBytes(decoded.build.data(), decoded.build.size())
        || !reader.readBytes(decoded.content.data(), decoded.content.size())
        || !reader.readU32(decoded.capabilities) || !reader.readU32(decoded.requiredCapabilities)
        || reader.remaining() != 0) {
        return false;
    }
    if (decoded.game != static_cast<std::uint8_t>(GameId::Pikmin1)
        && decoded.game != static_cast<std::uint8_t>(GameId::Pikmin2)) {
        return false;
    }
    info = decoded;
    return true;
}

CompatibilityField compareCompatibility(const CompatibilityInfo& host, const CompatibilityInfo& guest)
{
    if (host.game != guest.game) return CompatibilityField::Game;
    if (host.region != guest.region) return CompatibilityField::Region;
    if (host.build != guest.build) return CompatibilityField::Build;
    if (host.content != guest.content) return CompatibilityField::Content;
    if ((guest.capabilities & host.requiredCapabilities) != host.requiredCapabilities
        || (host.capabilities & guest.requiredCapabilities) != guest.requiredCapabilities) {
        return CompatibilityField::Capabilities;
    }
    return CompatibilityField::None;
}

const char* compatibilityFieldString(CompatibilityField field)
{
    switch (field) {
    case CompatibilityField::None:         return "none";
    case CompatibilityField::Game:         return "game";
    case CompatibilityField::Region:       return "region";
    case CompatibilityField::Build:        return "build";
    case CompatibilityField::Content:      return "content";
    case CompatibilityField::Capabilities: return "capabilities";
    }
    return "invalid_field";
}

CompatibilityCheck::CompatibilityCheck(Role role, const CompatibilityInfo& local, std::uint64_t timeoutMs)
    : mRole(role), mLocal(local), mTimeoutMs(timeoutMs)
{
}

void CompatibilityCheck::reset()
{
    mState = CompatibilityState::Idle;
    mMismatch = CompatibilityField::None;
    mPeer = CompatibilityInfo{};
}

JoinAction CompatibilityCheck::fail()
{
    mState = CompatibilityState::Failed;
    return JoinAction::Close;
}

JoinAction CompatibilityCheck::start(std::uint64_t nowMs, std::vector<std::uint8_t>& output)
{
    reset();
    mDeadlineMs = nowMs + mTimeoutMs;
    if (mRole == Role::Host) {
        mState = CompatibilityState::AwaitingOffer;
        return JoinAction::None;
    }
    std::vector<std::uint8_t> payload;
    if (!encodeCompatibilityInfo(mLocal, payload) || !encodeControl(MessageType::CompatibilityOffer, payload, output))
        return fail();
    mState = CompatibilityState::AwaitingResult;
    return JoinAction::Send;
}

JoinAction CompatibilityCheck::onMessage(Delivery delivery, const std::uint8_t* data, std::size_t size,
                                         std::vector<std::uint8_t>& output)
{
    if (mState == CompatibilityState::Compatible) return JoinAction::Forward;

    Message message;
    if (mRole == Role::Host) {
        if (mState != CompatibilityState::AwaitingOffer) return fail();
        if (!decodeControl(delivery, data, size, MessageType::CompatibilityOffer, kCompatibilityInfoBytes, message)
            || !decodeCompatibilityInfo(message.payload.data(), message.payload.size(), mPeer)) {
            return fail();
        }
        mMismatch = compareCompatibility(mLocal, mPeer);
        std::vector<std::uint8_t> payload{static_cast<std::uint8_t>(mMismatch)};
        std::vector<std::uint8_t> info;
        if (!encodeCompatibilityInfo(mLocal, info)) return fail();
        payload.insert(payload.end(), info.begin(), info.end());
        if (!encodeControl(MessageType::CompatibilityResult, payload, output)) return fail();
        if (mMismatch != CompatibilityField::None) {
            mState = CompatibilityState::Incompatible;
            return JoinAction::SendAndClose;
        }
        mState = CompatibilityState::Compatible;
        return JoinAction::Send;
    }

    if (mState != CompatibilityState::AwaitingResult) return fail();
    if (!decodeControl(delivery, data, size, MessageType::CompatibilityResult, kResultBytes, message)
        || message.payload[0] > static_cast<std::uint8_t>(CompatibilityField::Capabilities)
        || !decodeCompatibilityInfo(message.payload.data() + 1, kCompatibilityInfoBytes, mPeer)) {
        return fail();
    }
    const auto verdict = static_cast<CompatibilityField>(message.payload[0]);
    // The guest checks the verdict itself: a host must not wave through a
    // pair that its own info shows to be incompatible.
    const CompatibilityField local = compareCompatibility(mPeer, mLocal);
    if (verdict != local) return fail();
    mMismatch = verdict;
    if (verdict != CompatibilityField::None) {
        mState = CompatibilityState::Incompatible;
        return JoinAction::Close;
    }
    mState = CompatibilityState::Compatible;
    return JoinAction::None;
}

JoinAction CompatibilityCheck::tick(std::uint64_t nowMs)
{
    const bool waiting =
        mState == CompatibilityState::AwaitingOffer || mState == CompatibilityState::AwaitingResult;
    if (waiting && nowMs >= mDeadlineMs) return fail();
    return JoinAction::None;
}

} // namespace nectar::net
