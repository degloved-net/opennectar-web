#include "nectar_net/sim_network.h"

#include <algorithm>
#include <deque>
#include <vector>

namespace nectar::net {

namespace {

struct InFlight {
    std::uint64_t deliverAtMs = 0;
    std::uint64_t order = 0; // ties keep send order
    TransportEvent event;
};

} // namespace

struct SimulatedNetwork::State {
    std::uint64_t rng = 0;
    std::uint64_t nowMs = 0;
    std::uint64_t serial = 0;
    std::uint64_t sent = 0;
    std::uint64_t dropped = 0;
    std::uint64_t duplicated = 0;
    bool hostListening = false;
    bool connected = false;
    // [0] travels host -> guest, [1] guest -> host.
    LinkConditions conditions[2];
    std::vector<InFlight> flight[2];
    std::uint64_t lastReliableAt[2] = {0, 0};
    std::deque<TransportEvent> inbox[2]; // [0] host inbox, [1] guest inbox
    bool alive[2] = {false, false};

    std::uint32_t random()
    {
        rng ^= rng << 13;
        rng ^= rng >> 7;
        rng ^= rng << 17;
        return static_cast<std::uint32_t>(rng >> 16);
    }

    void post(int direction, TransportEvent event, bool reliable)
    {
        const LinkConditions& c = conditions[direction];
        ++sent;
        const auto delay = [&]() {
            return nowMs + c.latencyMs + (c.jitterMs == 0 ? 0 : random() % (c.jitterMs + 1));
        };
        if (reliable) {
            // In order and lossless: never earlier than the previous one.
            const std::uint64_t at = std::max(delay(), lastReliableAt[direction]);
            lastReliableAt[direction] = at;
            flight[direction].push_back({at, serial++, std::move(event)});
            return;
        }
        if (c.lossPerMille != 0 && random() % 1000 < c.lossPerMille) {
            ++dropped;
            return;
        }
        if (c.duplicatePerMille != 0 && random() % 1000 < c.duplicatePerMille) {
            ++duplicated;
            flight[direction].push_back({delay(), serial++, event});
        }
        flight[direction].push_back({delay(), serial++, std::move(event)});
    }

    void deliver()
    {
        for (int direction = 0; direction < 2; ++direction) {
            auto& queue = flight[direction];
            std::stable_sort(queue.begin(), queue.end(), [](const InFlight& a, const InFlight& b) {
                return a.deliverAtMs != b.deliverAtMs ? a.deliverAtMs < b.deliverAtMs : a.order < b.order;
            });
            std::size_t due = 0;
            while (due < queue.size() && queue[due].deliverAtMs <= nowMs) ++due;
            const int receiver = direction == 0 ? 1 : 0;
            for (std::size_t i = 0; i < due; ++i)
                if (connected) inbox[receiver].push_back(std::move(queue[i].event));
            queue.erase(queue.begin(), queue.begin() + static_cast<std::ptrdiff_t>(due));
        }
    }

    void hangUp(int from)
    {
        if (!connected) return;
        connected = false;
        for (auto& queue : flight) queue.clear();
        TransportEvent event;
        event.type = TransportEventType::Disconnected;
        event.detail = "peer left";
        inbox[from == 0 ? 1 : 0].push_back(std::move(event));
    }
};

namespace {

class SimEnd final : public ITransport {
public:
    SimEnd(std::shared_ptr<SimulatedNetwork::State> state, int side) : mState(std::move(state)), mSide(side)
    {
        mState->alive[mSide] = true;
    }
    ~SimEnd() override
    {
        close();
        mState->alive[mSide] = false;
    }

    bool listen(const Endpoint&) override
    {
        if (mSide != 0) return false;
        mState->hostListening = true;
        return true;
    }

    bool connect(const Endpoint&) override
    {
        if (mSide != 1) return false;
        if (!mState->hostListening || mState->connected) {
            TransportEvent event;
            event.type = TransportEventType::Disconnected;
            event.detail = mState->connected ? kRoomFullReason : "no route";
            mState->inbox[1].push_back(std::move(event));
            return true;
        }
        mState->connected = true;
        TransportEvent guest;
        guest.type = TransportEventType::Connected;
        mState->inbox[1].push_back(guest);
        TransportEvent host;
        host.type = TransportEventType::Connected;
        host.peerAddress = "192.0.2.77";
        mState->inbox[0].push_back(std::move(host));
        return true;
    }

    bool send(Delivery delivery, const std::uint8_t* data, std::size_t size) override
    {
        if (!mState->connected || (data == nullptr && size != 0)) return false;
        TransportEvent event;
        event.type = TransportEventType::Message;
        event.delivery = delivery;
        event.payload.assign(data, data + size);
        mState->post(mSide == 0 ? 0 : 1, std::move(event), delivery == Delivery::ReliableOrdered);
        return true;
    }

    bool poll(TransportEvent& event) override
    {
        auto& inbox = mState->inbox[mSide];
        if (inbox.empty()) return false;
        event = std::move(inbox.front());
        inbox.pop_front();
        return true;
    }

    void disconnectPeer(bool) override { mState->hangUp(mSide); }

    void close() override
    {
        mState->hangUp(mSide);
        if (mSide == 0) mState->hostListening = false;
    }

    TransportState state() const override
    {
        if (mState->connected) return TransportState::Connected;
        return mSide == 0 && mState->hostListening ? TransportState::Listening : TransportState::Closed;
    }

    bool stats(TransportStats& output) const override
    {
        output = TransportStats{};
        if (!mState->connected) return false;
        const LinkConditions& out = mState->conditions[mSide == 0 ? 0 : 1];
        const LinkConditions& in = mState->conditions[mSide == 0 ? 1 : 0];
        output.rttMs = static_cast<std::int32_t>(out.latencyMs + in.latencyMs + (out.jitterMs + in.jitterMs) / 2);
        output.localQuality = 1.0f - static_cast<float>(in.lossPerMille) / 1000.0f;
        output.remoteQuality = 1.0f - static_cast<float>(out.lossPerMille) / 1000.0f;
        output.pendingReliableBytes = 0;
        output.pendingUnreliableBytes = 0;
        return true;
    }

private:
    std::shared_ptr<SimulatedNetwork::State> mState;
    int mSide;
};

} // namespace

SimulatedNetwork::SimulatedNetwork(std::uint64_t seed) : mState(std::make_shared<State>())
{
    mState->rng = seed == 0 ? 0x9e3779b97f4a7c15ULL : seed * 0x9e3779b97f4a7c15ULL;
}

SimulatedNetwork::~SimulatedNetwork() = default;

std::unique_ptr<ITransport> SimulatedNetwork::createHost() { return std::make_unique<SimEnd>(mState, 0); }
std::unique_ptr<ITransport> SimulatedNetwork::createGuest() { return std::make_unique<SimEnd>(mState, 1); }

void SimulatedNetwork::setHostToGuest(const LinkConditions& conditions) { mState->conditions[0] = conditions; }
void SimulatedNetwork::setGuestToHost(const LinkConditions& conditions) { mState->conditions[1] = conditions; }

void SimulatedNetwork::advance(std::uint64_t nowMs)
{
    if (nowMs > mState->nowMs) mState->nowMs = nowMs;
    mState->deliver();
}

std::uint64_t SimulatedNetwork::now() const { return mState->nowMs; }
std::uint64_t SimulatedNetwork::sent() const { return mState->sent; }
std::uint64_t SimulatedNetwork::dropped() const { return mState->dropped; }
std::uint64_t SimulatedNetwork::duplicated() const { return mState->duplicated; }

} // namespace nectar::net
