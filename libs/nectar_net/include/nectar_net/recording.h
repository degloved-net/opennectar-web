#pragma once

#include "nectar_net/transport.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace nectar::net {

// Session capture for desync forensics and deterministic regression tests.
// Record only after GatedTransport reports Connected: the handshake (and so
// the join secret) never enters a recording. Format, big endian:
//   "NNRC" | version u16 | entries: time u64 | flags u8 | length u32 | bytes
// flags: bit0 inbound, bit1 reliable. Bounded: recording stops (and says so)
// at maxBytes instead of growing without limit.

enum class RecordDirection : std::uint8_t { Outbound = 0, Inbound = 1 };

struct RecordedMessage {
    std::uint64_t timeMs = 0;
    RecordDirection direction = RecordDirection::Inbound;
    Delivery delivery = Delivery::ReliableOrdered;
    std::vector<std::uint8_t> payload;
};

constexpr std::uint16_t kRecordingVersion = 1;
constexpr std::size_t kMaxRecordedMessageBytes = 4U * 1024U * 1024U;

class SessionRecorder {
public:
    explicit SessionRecorder(std::size_t maxBytes = 64U * 1024U * 1024U);

    // False once the size cap is reached; later messages are dropped.
    bool record(std::uint64_t timeMs, RecordDirection direction, Delivery delivery, const std::uint8_t* data,
                std::size_t size);

    const std::vector<std::uint8_t>& bytes() const { return mBytes; }
    bool truncated() const { return mTruncated; }

private:
    std::size_t mMaxBytes;
    std::vector<std::uint8_t> mBytes;
    std::uint64_t mLastTime = 0;
    bool mTruncated = false;
};

// Strict parser: rejects bad magic/version, oversized entries, times going
// backwards and trailing bytes.
bool parseRecording(const std::uint8_t* data, std::size_t size, std::vector<RecordedMessage>& messages);

// Plays the inbound side of a recording back as a connected transport:
// messages become available once advance() reaches their time. Sends are
// accepted and counted, never transmitted.
class ReplayTransport final : public ITransport {
public:
    explicit ReplayTransport(std::vector<RecordedMessage> messages);

    void advance(std::uint64_t nowMs);
    std::size_t remaining() const { return mPending.size(); }
    std::size_t sentCount() const { return mSent; }

    bool listen(const Endpoint& endpoint) override;
    bool connect(const Endpoint& endpoint) override;
    bool send(Delivery delivery, const std::uint8_t* data, std::size_t size) override;
    bool poll(TransportEvent& event) override;
    void disconnectPeer(bool flushReliable) override;
    void close() override;
    TransportState state() const override;
    bool stats(TransportStats& output) const override;

private:
    std::deque<RecordedMessage> mPending;
    std::deque<TransportEvent> mReady;
    std::size_t mSent = 0;
    bool mOpen = true;
    bool mAnnounced = false;
};

} // namespace nectar::net
