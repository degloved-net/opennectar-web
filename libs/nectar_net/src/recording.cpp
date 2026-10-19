#include "nectar_net/recording.h"

#include "nectar_net/byte_io.h"

namespace nectar::net {

namespace {

constexpr std::uint8_t kMagic[4] = {'N', 'N', 'R', 'C'};
constexpr std::size_t kHeaderBytes = 6;
constexpr std::size_t kEntryHeaderBytes = 13;
constexpr std::uint8_t kFlagInbound = 0x01;
constexpr std::uint8_t kFlagReliable = 0x02;

} // namespace

SessionRecorder::SessionRecorder(std::size_t maxBytes) : mMaxBytes(maxBytes)
{
    ByteWriter writer(kHeaderBytes);
    writer.writeBytes(kMagic, sizeof(kMagic));
    writer.writeU16(kRecordingVersion);
    mBytes = writer.bytes();
}

bool SessionRecorder::record(std::uint64_t timeMs, RecordDirection direction, Delivery delivery,
                             const std::uint8_t* data, std::size_t size)
{
    if (mTruncated) return false;
    if ((data == nullptr && size != 0) || size > kMaxRecordedMessageBytes
        || mBytes.size() + kEntryHeaderBytes + size > mMaxBytes) {
        mTruncated = true;
        return false;
    }
    // Keep times monotonic so the parser can trust ordering.
    if (timeMs < mLastTime) timeMs = mLastTime;
    mLastTime = timeMs;
    ByteWriter writer(kEntryHeaderBytes + size);
    writer.writeU64(timeMs);
    std::uint8_t flags = direction == RecordDirection::Inbound ? kFlagInbound : 0;
    if (delivery == Delivery::ReliableOrdered) flags |= kFlagReliable;
    writer.writeU8(flags);
    writer.writeU32(static_cast<std::uint32_t>(size));
    writer.writeBytes(data, size);
    const std::vector<std::uint8_t>& entry = writer.bytes();
    mBytes.insert(mBytes.end(), entry.begin(), entry.end());
    return true;
}

bool parseRecording(const std::uint8_t* data, std::size_t size, std::vector<RecordedMessage>& messages)
{
    if (data == nullptr || size < kHeaderBytes) return false;
    ByteReader reader(data, size);
    std::uint8_t magic[4];
    std::uint16_t version = 0;
    if (!reader.readBytes(magic, 4) || !reader.readU16(version)) return false;
    for (int i = 0; i < 4; ++i)
        if (magic[i] != kMagic[i]) return false;
    if (version != kRecordingVersion) return false;
    std::vector<RecordedMessage> parsed;
    std::uint64_t lastTime = 0;
    while (reader.remaining() != 0) {
        RecordedMessage message;
        std::uint8_t flags = 0;
        std::uint32_t length = 0;
        if (!reader.readU64(message.timeMs) || !reader.readU8(flags) || !reader.readU32(length)) return false;
        if ((flags & ~(kFlagInbound | kFlagReliable)) != 0 || length > kMaxRecordedMessageBytes
            || length > reader.remaining() || message.timeMs < lastTime) {
            return false;
        }
        lastTime = message.timeMs;
        message.direction = (flags & kFlagInbound) != 0 ? RecordDirection::Inbound : RecordDirection::Outbound;
        message.delivery = (flags & kFlagReliable) != 0 ? Delivery::ReliableOrdered : Delivery::Unreliable;
        message.payload.resize(length);
        if (!reader.readBytes(message.payload.data(), length)) return false;
        parsed.push_back(std::move(message));
    }
    messages = std::move(parsed);
    return true;
}

ReplayTransport::ReplayTransport(std::vector<RecordedMessage> messages)
{
    for (RecordedMessage& message : messages)
        if (message.direction == RecordDirection::Inbound) mPending.push_back(std::move(message));
}

void ReplayTransport::advance(std::uint64_t nowMs)
{
    if (!mOpen) return;
    if (!mAnnounced) {
        TransportEvent connected;
        connected.type = TransportEventType::Connected;
        mReady.push_back(std::move(connected));
        mAnnounced = true;
    }
    while (!mPending.empty() && mPending.front().timeMs <= nowMs) {
        TransportEvent event;
        event.type = TransportEventType::Message;
        event.delivery = mPending.front().delivery;
        event.payload = std::move(mPending.front().payload);
        mReady.push_back(std::move(event));
        mPending.pop_front();
    }
}

bool ReplayTransport::listen(const Endpoint&) { return true; }
bool ReplayTransport::connect(const Endpoint&) { return true; }

bool ReplayTransport::send(Delivery, const std::uint8_t*, std::size_t)
{
    if (!mOpen) return false;
    ++mSent;
    return true;
}

bool ReplayTransport::poll(TransportEvent& event)
{
    if (mReady.empty()) return false;
    event = std::move(mReady.front());
    mReady.pop_front();
    return true;
}

void ReplayTransport::disconnectPeer(bool) { close(); }

void ReplayTransport::close()
{
    mOpen = false;
    mPending.clear();
}

TransportState ReplayTransport::state() const { return mOpen ? TransportState::Connected : TransportState::Closed; }

bool ReplayTransport::stats(TransportStats& output) const
{
    output = TransportStats{};
    return mOpen;
}

} // namespace nectar::net
