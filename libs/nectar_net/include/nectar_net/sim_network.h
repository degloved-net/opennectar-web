#pragma once

#include "nectar_net/transport.h"

#include <cstdint>
#include <memory>

namespace nectar::net {

// Deterministic in-memory network between one host and one guest, for tests
// and soak runs: latency, jitter, loss, duplication and reordering per
// direction, driven by an explicit clock and a fixed seed. Reliable messages
// keep their order and are never lost, like GNS; unreliable ones may be
// dropped, duplicated or overtaken.

struct LinkConditions {
    std::uint32_t latencyMs = 0;
    std::uint32_t jitterMs = 0;          // uniform extra delay 0..jitter
    std::uint16_t lossPerMille = 0;      // unreliable only
    std::uint16_t duplicatePerMille = 0; // unreliable only
};

class SimulatedNetwork {
public:
    explicit SimulatedNetwork(std::uint64_t seed = 1);
    ~SimulatedNetwork();

    SimulatedNetwork(const SimulatedNetwork&) = delete;
    SimulatedNetwork& operator=(const SimulatedNetwork&) = delete;

    // The two ends. Each may be wrapped in GatedTransport. The network must
    // outlive them.
    std::unique_ptr<ITransport> createHost();
    std::unique_ptr<ITransport> createGuest();

    void setHostToGuest(const LinkConditions& conditions);
    void setGuestToHost(const LinkConditions& conditions);

    // Moves the clock; datagrams whose delivery time has come are queued on
    // the receiving end. Time never goes backwards.
    void advance(std::uint64_t nowMs);
    std::uint64_t now() const;

    // Delivery counters for assertions.
    std::uint64_t sent() const;
    std::uint64_t dropped() const;
    std::uint64_t duplicated() const;

    struct State;

private:
    std::shared_ptr<State> mState;
};

} // namespace nectar::net
