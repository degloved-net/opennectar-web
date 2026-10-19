#pragma once

#include "nectar_net/transport.h"

#include <memory>

namespace nectar::net {

// Direct encrypted UDP transport backed by Valve GameNetworkingSockets.
// This class is built only when NECTAR_NET_ENABLE_GNS is enabled. GNS
// encrypts packets in transit but does not authenticate the peer: wrap it in
// GatedTransport, which checks the NECTAR1 join secret (ADR-0002).
class GnsTransport final : public ITransport {
public:
    GnsTransport();
    ~GnsTransport() override;

    GnsTransport(const GnsTransport&) = delete;
    GnsTransport& operator=(const GnsTransport&) = delete;

    bool listen(const Endpoint& endpoint) override;
    bool connect(const Endpoint& endpoint) override;
    bool send(Delivery delivery, const std::uint8_t* data, std::size_t size) override;
    bool poll(TransportEvent& event) override;
    void disconnectPeer(bool flushReliable) override;
    void close() override;
    TransportState state() const override;
    bool stats(TransportStats& output) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace nectar::net
