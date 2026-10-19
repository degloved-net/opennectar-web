#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nectar::net {

enum class Delivery {
    ReliableOrdered,
    Unreliable,
};

enum class TransportState {
    Closed,
    Listening,
    Connecting,
    Connected,
    Failed,
};

enum class TransportEventType {
    Connected,
    Disconnected,
    Message,
    Error,
};

// Detail of the Disconnected/Error event a guest gets when the host already
// has its one guest. Transports must use exactly this text.
constexpr const char* kRoomFullReason = "room_full";

struct Endpoint {
    std::string address;
    std::uint16_t port = 0;
};

struct TransportEvent {
    TransportEventType type = TransportEventType::Error;
    Delivery delivery       = Delivery::ReliableOrdered;
    std::vector<std::uint8_t> payload;
    std::string detail;
    // Connected only: the peer's IP without port, for per-origin rate limits.
    // Private data: keep it in memory, never log or persist it.
    std::string peerAddress;
};

// Live link metrics. Fields are -1 when the transport cannot measure them yet.
struct TransportStats {
    std::int32_t rttMs = -1;
    // Fraction of packets delivered, 0..1, as seen locally and by the peer.
    float localQuality  = -1.0f;
    float remoteQuality = -1.0f;
    std::int32_t sendRateBytesPerSecond = -1;
    std::int32_t pendingReliableBytes   = -1;
    std::int32_t pendingUnreliableBytes = -1;
    float outBytesPerSecond = -1.0f;
    float inBytesPerSecond  = -1.0f;
};

class ITransport {
public:
    virtual ~ITransport() = default;

    virtual bool listen(const Endpoint& endpoint) = 0;
    virtual bool connect(const Endpoint& endpoint) = 0;
    virtual bool send(Delivery delivery, const std::uint8_t* data, std::size_t size) = 0;
    virtual bool poll(TransportEvent& event) = 0;
    // Drops only the current peer. A host keeps listening for a new guest.
    // flushReliable asks the transport to deliver queued reliable data first.
    virtual void disconnectPeer(bool flushReliable) = 0;
    virtual void close() = 0;
    virtual TransportState state() const = 0;
    // Fills metrics for the current peer; false when there is no connection.
    virtual bool stats(TransportStats& output) const = 0;
};

} // namespace nectar::net
