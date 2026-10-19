#include "nectar_net/gated_transport.h"

#include "nectar_net/secure_memory.h"

#include <chrono>
#include <utility>

namespace nectar::net {

namespace {

std::uint64_t steadyMilliseconds()
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

void wipe(std::vector<std::uint8_t>& bytes)
{
    secureWipe(bytes.data(), bytes.size());
    bytes.clear();
}

} // namespace

GatedTransport::GatedTransport(std::unique_ptr<ITransport> inner, Role role, const JoinCredentials& credentials,
                               Clock clock, std::uint64_t timeoutMs)
    : mInner(std::move(inner)),
      mRole(role),
      mClock(clock ? std::move(clock) : Clock(steadyMilliseconds)),
      mHost(credentials, timeoutMs),
      mGuest(credentials, timeoutMs)
{
}

GatedTransport::~GatedTransport()
{
    if (mInner) mInner->close();
}

bool GatedTransport::listen(const Endpoint& endpoint)
{
    if (mRole != Role::Host || !mInner) return false;
    mReady.clear();
    mHost.onDisconnected();
    if (mCompat) mCompat->reset();
    return mInner->listen(endpoint);
}

bool GatedTransport::connect(const Endpoint& endpoint)
{
    if (mRole != Role::Guest || !mInner) return false;
    mReady.clear();
    mGuest.onDisconnected();
    if (mCompat) mCompat->reset();
    return mInner->connect(endpoint);
}

void GatedTransport::requireCompatibility(const CompatibilityInfo& local)
{
    mCompat = std::make_unique<CompatibilityCheck>(
        mRole == Role::Host ? CompatibilityCheck::Role::Host : CompatibilityCheck::Role::Guest, local);
}

bool GatedTransport::sessionReady() const
{
    return joinState() == JoinState::Authenticated
        && (!mCompat || mCompat->state() == CompatibilityState::Compatible);
}

JoinState GatedTransport::joinState() const
{
    return mRole == Role::Host ? mHost.state() : mGuest.state();
}

bool GatedTransport::send(Delivery delivery, const std::uint8_t* data, std::size_t size)
{
    if (!mInner || !sessionReady()) return false;
    return mInner->send(delivery, data, size);
}

bool GatedTransport::stats(TransportStats& output) const
{
    if (!mInner) {
        output = TransportStats{};
        return false;
    }
    return mInner->stats(output);
}

GatedTransport::Origin& GatedTransport::origin(const std::string& address, std::uint64_t now)
{
    for (Origin& entry : mOrigins) {
        if (entry.address == address) {
            entry.lastSeenMs = now;
            return entry;
        }
    }
    if (mOrigins.size() >= kMaxTrackedOrigins) {
        // Forget the least recently seen origin; a ban still running is kept
        // over an idle entry so a flood cannot evict it cheaply.
        std::size_t victim = 0;
        for (std::size_t i = 1; i < mOrigins.size(); ++i) {
            const bool iBanned = mOrigins[i].bannedUntilMs > now;
            const bool vBanned = mOrigins[victim].bannedUntilMs > now;
            if ((vBanned && !iBanned) || (vBanned == iBanned && mOrigins[i].lastSeenMs < mOrigins[victim].lastSeenMs))
                victim = i;
        }
        mOrigins.erase(mOrigins.begin() + static_cast<std::ptrdiff_t>(victim));
    }
    Origin entry;
    entry.address = address;
    entry.lastSeenMs = now;
    mOrigins.push_back(std::move(entry));
    return mOrigins.back();
}

void GatedTransport::noteFailure(std::uint64_t now)
{
    if (mRole != Role::Host || mPeerAddress.empty()) return;
    Origin& entry = origin(mPeerAddress, now);
    ++entry.failures;
    if (entry.failures >= kFailuresBeforeBan) {
        std::uint64_t ban = kFirstBanMs;
        for (std::uint32_t extra = entry.failures - kFailuresBeforeBan; extra != 0 && ban < kMaxBanMs; --extra) ban *= 2;
        entry.bannedUntilMs = now + (ban < kMaxBanMs ? ban : kMaxBanMs);
    }
}

std::size_t GatedTransport::bannedOrigins() const
{
    const std::uint64_t now = mClock();
    std::size_t count = 0;
    for (const Origin& entry : mOrigins) count += entry.bannedUntilMs > now ? 1U : 0U;
    return count;
}

TransportState GatedTransport::state() const
{
    if (!mInner) return TransportState::Closed;
    const TransportState inner = mInner->state();
    if (inner == TransportState::Connected && !sessionReady()) return TransportState::Connecting;
    return inner;
}

void GatedTransport::surface(TransportEventType type, const char* detail)
{
    TransportEvent event;
    event.type = type;
    if (detail != nullptr) event.detail = detail;
    mReady.push_back(std::move(event));
}

void GatedTransport::dropPeer(JoinAction action, JoinError error)
{
    mLastError = error;
    mInner->disconnectPeer(action == JoinAction::SendAndClose);
    if (mRole == Role::Host) {
        // Invisible to the session layer: the room simply keeps waiting.
        ++mRejectedPeers;
        // A friend on another version knew the secret: no penalty for that.
        if (error != JoinError::RateLimited && error != JoinError::Incompatible) noteFailure(mClock());
        mPeerAddress.clear();
        mHost.onDisconnected();
    } else {
        std::string detail = joinErrorString(error);
        if (error == JoinError::Incompatible && mCompat) {
            detail += ':';
            detail += compatibilityFieldString(mCompat->mismatch());
        }
        surface(TransportEventType::Error, detail.c_str());
    }
}

void GatedTransport::onAuthenticated(std::uint64_t now)
{
    if (mRole == Role::Host && !mPeerAddress.empty()) {
        Origin& entry = origin(mPeerAddress, now);
        entry.failures = 0;
        entry.bannedUntilMs = 0;
    }
    if (!mCompat) {
        surface(TransportEventType::Connected, nullptr);
        return;
    }
    std::vector<std::uint8_t> offer;
    const JoinAction action = mCompat->start(now, offer);
    if (action == JoinAction::Send && !mInner->send(Delivery::ReliableOrdered, offer.data(), offer.size())) {
        dropPeer(JoinAction::Close, JoinError::UnexpectedMessage);
    } else if (action == JoinAction::Close) {
        dropPeer(JoinAction::Close, JoinError::Malformed);
    }
}

void GatedTransport::handleCompatibility(TransportEvent& event, std::vector<std::uint8_t>& output)
{
    const JoinAction action =
        mCompat->onMessage(event.delivery, event.payload.data(), event.payload.size(), output);
    const bool incompatible = mCompat->state() == CompatibilityState::Incompatible;
    switch (action) {
    case JoinAction::Send:
        if (!mInner->send(Delivery::ReliableOrdered, output.data(), output.size())) {
            dropPeer(JoinAction::Close, JoinError::UnexpectedMessage);
            return;
        }
        break;
    case JoinAction::SendAndClose:
        mInner->send(Delivery::ReliableOrdered, output.data(), output.size());
        dropPeer(action, JoinError::Incompatible);
        return;
    case JoinAction::Close:
        dropPeer(action, incompatible ? JoinError::Incompatible : JoinError::Malformed);
        return;
    case JoinAction::None:
    case JoinAction::Forward:
        break;
    }
    if (mCompat->state() == CompatibilityState::Compatible) surface(TransportEventType::Connected, nullptr);
}

void GatedTransport::apply(JoinAction action, const std::vector<std::uint8_t>& output)
{
    const JoinError error = mRole == Role::Host ? mHost.error() : mGuest.error();
    switch (action) {
    case JoinAction::None:
    case JoinAction::Forward:
        return;
    case JoinAction::Send:
        if (!mInner->send(Delivery::ReliableOrdered, output.data(), output.size())) {
            dropPeer(JoinAction::Close, JoinError::UnexpectedMessage);
        }
        return;
    case JoinAction::SendAndClose:
        mInner->send(Delivery::ReliableOrdered, output.data(), output.size());
        dropPeer(action, error);
        return;
    case JoinAction::Close:
        dropPeer(action, error);
        return;
    }
}

void GatedTransport::handle(TransportEvent& event)
{
    const std::uint64_t now = mClock();
    std::vector<std::uint8_t> output;

    switch (event.type) {
    case TransportEventType::Connected:
        if (mRole == Role::Host) {
            mPeerAddress = std::move(event.peerAddress);
            if (!mPeerAddress.empty()) {
                const Origin& entry = origin(mPeerAddress, now);
                if (entry.bannedUntilMs > now) {
                    dropPeer(JoinAction::Close, JoinError::RateLimited);
                    break;
                }
            }
            mHost.onConnected(now);
        } else {
            apply(mGuest.onConnected(now, output), output);
        }
        break;

    case TransportEventType::Message: {
        if (joinState() == JoinState::Authenticated && mCompat
            && mCompat->state() != CompatibilityState::Compatible) {
            handleCompatibility(event, output);
            break;
        }
        const bool wasAuthenticated = joinState() == JoinState::Authenticated;
        const JoinAction action = mRole == Role::Host
            ? mHost.onMessage(event.delivery, event.payload.data(), event.payload.size(), output)
            : mGuest.onMessage(event.delivery, event.payload.data(), event.payload.size());
        if (action == JoinAction::Forward) {
            mReady.push_back(std::move(event));
            break;
        }
        wipe(event.payload);
        apply(action, output);
        if (!wasAuthenticated && joinState() == JoinState::Authenticated) onAuthenticated(now);
        break;
    }

    case TransportEventType::Disconnected:
    case TransportEventType::Error: {
        const bool wasAuthenticated = joinState() == JoinState::Authenticated;
        const bool wasReady = sessionReady();
        if (mRole == Role::Host) {
            // Hanging up during quarantine also counts against the origin, or
            // a scanner could hold the room by connecting and leaving.
            if (!wasAuthenticated) noteFailure(now);
            mPeerAddress.clear();
            mHost.onDisconnected();
            // A peer the session never saw connect leaving is not an event.
            if (wasReady || event.type == TransportEventType::Error) mReady.push_back(std::move(event));
        } else {
            if (mGuest.state() != JoinState::Rejected) mGuest.onDisconnected();
            mReady.push_back(std::move(event));
        }
        break;
    }
    }
    wipe(output);
}

bool GatedTransport::poll(TransportEvent& event)
{
    if (!mInner) return false;
    TransportEvent incoming;
    while (mReady.empty() && mInner->poll(incoming)) handle(incoming);

    if (mReady.empty()) {
        const std::uint64_t now = mClock();
        const JoinAction action = mRole == Role::Host ? mHost.tick(now) : mGuest.tick(now);
        if (action == JoinAction::Close) {
            dropPeer(action, JoinError::Timeout);
        } else if (mCompat && joinState() == JoinState::Authenticated && mCompat->tick(now) == JoinAction::Close) {
            dropPeer(JoinAction::Close, JoinError::Timeout);
        }
    }

    if (mReady.empty()) return false;
    event = std::move(mReady.front());
    mReady.pop_front();
    return true;
}

void GatedTransport::disconnectPeer(bool flushReliable)
{
    if (!mInner) return;
    mInner->disconnectPeer(flushReliable);
    mHost.onDisconnected();
    mGuest.onDisconnected();
}

void GatedTransport::close()
{
    if (mInner) mInner->close();
    mReady.clear();
    mOrigins.clear();
    mPeerAddress.clear();
    mHost.onDisconnected();
    mGuest.onDisconnected();
    if (mCompat) mCompat->reset();
}

} // namespace nectar::net
