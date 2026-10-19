#include "nectar_net/room.h"

#include "nectar_net/secure_memory.h"
#include "nectar_net/secure_random.h"

#ifdef NECTAR_NET_HAS_GNS
#include "nectar_net/gns_transport.h"
#endif

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace nectar::net {

namespace {

constexpr std::uint8_t kPriorityManual = 15;
constexpr int kListenAttempts = 5;

bool allZero(const std::array<std::uint8_t, 16>& bytes)
{
    return std::all_of(bytes.begin(), bytes.end(), [](std::uint8_t b) { return b == 0; });
}

} // namespace

TransportFactory defaultTransportFactory()
{
#ifdef NECTAR_NET_HAS_GNS
    return []() -> std::unique_ptr<ITransport> { return std::make_unique<GnsTransport>(); };
#else
    return []() -> std::unique_ptr<ITransport> { return nullptr; };
#endif
}

std::string endpointAddressText(const DirectEndpoint& endpoint)
{
    char text[48];
    if (endpoint.family == DirectEndpoint::Family::Ipv4) {
        std::snprintf(text, sizeof(text), "%u.%u.%u.%u", endpoint.address[0], endpoint.address[1], endpoint.address[2],
                      endpoint.address[3]);
        return text;
    }
    std::string result;
    for (std::size_t i = 0; i < 16; i += 2) {
        std::snprintf(text, sizeof(text), "%s%x", i == 0 ? "" : ":",
                      static_cast<unsigned>((endpoint.address[i] << 8) | endpoint.address[i + 1]));
        result += text;
    }
    return result;
}

const char* roomErrorString(RoomError error)
{
    switch (error) {
    case RoomError::None:                return "none";
    case RoomError::NoTransport:         return "no_transport";
    case RoomError::ListenFailed:        return "listen_failed";
    case RoomError::RandomSourceFailure: return "random_source_failure";
    case RoomError::NoEndpoint:          return "no_endpoint";
    }
    return "invalid_room_error";
}

const char* joinFailureString(JoinFailure failure)
{
    switch (failure) {
    case JoinFailure::None:         return "none";
    case JoinFailure::BadCode:      return "bad_code";
    case JoinFailure::Expired:      return "expired";
    case JoinFailure::WrongVersion: return "wrong_version";
    case JoinFailure::NoTransport:  return "no_transport";
    case JoinFailure::Unreachable:  return "unreachable";
    case JoinFailure::Refused:      return "refused";
    case JoinFailure::Incompatible: return "incompatible";
    case JoinFailure::RoomFull:     return "room_full";
    case JoinFailure::Lost:         return "lost";
    }
    return "invalid_join_failure";
}

// --- Host ------------------------------------------------------------------

HostRoom::HostRoom(TransportFactory factory, PublisherBackends backends)
    : mFactory(std::move(factory)), mPublisher(std::make_unique<PortPublisher>(std::move(backends)))
{
}

HostRoom::~HostRoom() { close(); }

std::string HostRoom::issueCode(std::uint64_t nowUnixSeconds)
{
    std::vector<DirectEndpoint> endpoints = mConfig.manualEndpoints;
    for (DirectEndpoint& endpoint : endpoints)
        if (endpoint.priority == 0) endpoint.priority = kPriorityManual;
    endpoints.insert(endpoints.end(), mPublished.endpoints.begin(), mPublished.endpoints.end());
    std::stable_sort(endpoints.begin(), endpoints.end(),
                     [](const DirectEndpoint& a, const DirectEndpoint& b) { return a.priority < b.priority; });
    if (endpoints.size() > ConnectionCode::kMaxEndpoints) endpoints.resize(ConnectionCode::kMaxEndpoints);
    mCode.endpoints = endpoints;
    mCode.protocolMajor = mConfig.protocolMajor;
    mCode.expiresUnixSeconds = nowUnixSeconds + mConfig.codeLifetimeSeconds;
    return mCode.endpoints.empty() ? std::string() : encodeConnectionCode(mCode);
}

RoomError HostRoom::open(const RoomConfig& config, std::uint64_t nowUnixSeconds, RoomStatus& status)
{
    close();
    mConfig = config;
    mCode = ConnectionCode{};
    if (!secureRandomBytes(mCode.sessionNonce.data(), mCode.sessionNonce.size())
        || !secureRandomBytes(mCode.joinSecret.data(), mCode.joinSecret.size()) || allZero(mCode.sessionNonce)
        || allZero(mCode.joinSecret)) {
        close();
        return RoomError::RandomSourceFailure;
    }

    std::uint16_t port = config.localPort;
    for (int attempt = 0; attempt < kListenAttempts && !mTransport; ++attempt) {
        if (config.localPort == 0) {
            std::uint16_t random = 0;
            if (!secureRandomBytes(reinterpret_cast<std::uint8_t*>(&random), sizeof(random))) {
                close();
                return RoomError::RandomSourceFailure;
            }
            port = static_cast<std::uint16_t>(49152U + random % 16384U);
        }
        std::unique_ptr<ITransport> inner = mFactory ? mFactory() : nullptr;
        if (!inner) {
            close();
            return RoomError::NoTransport;
        }
        auto gated = std::make_unique<GatedTransport>(std::move(inner), GatedTransport::Role::Host,
                                                      joinCredentialsFromCode(mCode));
        gated->requireCompatibility(config.compatibility);
        if (gated->listen({"", port})) mTransport = std::move(gated);
        if (config.localPort != 0) break; // a fixed port is not changed behind the user's back
    }
    if (!mTransport) {
        close();
        return RoomError::ListenFailed;
    }

    if (config.automaticPortMapping) {
        mPublished = mPublisher->publish(port, 7200, config.includeLanEndpoint);
    } else {
        PublisherBackends local = PublisherBackends::system();
        local.findGateway = nullptr;
        local.openUpnp = nullptr;
        PortPublisher addressesOnly(local);
        mPublished = addressesOnly.publish(port, 0, config.includeLanEndpoint);
    }

    status = RoomStatus{};
    status.localPort = port;
    status.reachability = mPublished.reachability;
    status.method = mPublished.method;
    status.code = issueCode(nowUnixSeconds);
    if (status.code.empty()) {
        close();
        status = RoomStatus{};
        return RoomError::NoEndpoint;
    }
    return RoomError::None;
}

bool HostRoom::maintain(std::uint64_t nowMs, std::uint64_t nowUnixSeconds, RoomStatus& status)
{
    if (!mTransport || !mPublisher->maintain(nowMs, mPublished)) return false;
    status.reachability = mPublished.reachability;
    status.code = issueCode(nowUnixSeconds);
    return true;
}

void HostRoom::close()
{
    if (mPublisher) mPublisher->withdraw();
    if (mTransport) mTransport->close();
    mTransport.reset();
    secureWipe(mCode.joinSecret.data(), mCode.joinSecret.size());
    secureWipe(mCode.sessionNonce.data(), mCode.sessionNonce.size());
    mCode = ConnectionCode{};
    mPublished = PublishResult{};
}

// --- Guest -----------------------------------------------------------------

GuestJoin::GuestJoin(TransportFactory factory) : mFactory(std::move(factory)) {}

JoinFailure GuestJoin::start(const std::string& code, const CompatibilityInfo& local, std::uint16_t protocolMajor,
                             std::uint64_t nowUnixSeconds, std::uint64_t nowMs)
{
    cancel();
    mLocal = local;
    mFailure = JoinFailure::None;
    const ConnectionCodeError decoded = decodeConnectionCode(code, mCode);
    if (decoded == ConnectionCodeError::UnsupportedVersion) {
        mFailure = JoinFailure::WrongVersion;
    } else if (decoded == ConnectionCodeError::Expired) {
        mFailure = JoinFailure::Expired;
    } else if (decoded != ConnectionCodeError::None) {
        mFailure = JoinFailure::BadCode;
    } else {
        const ConnectionCodeError valid = validateConnectionCode(mCode, protocolMajor, nowUnixSeconds);
        if (valid == ConnectionCodeError::InvalidProtocol)
            mFailure = JoinFailure::WrongVersion;
        else if (valid == ConnectionCodeError::Expired)
            mFailure = JoinFailure::Expired;
        else if (valid != ConnectionCodeError::None)
            mFailure = JoinFailure::BadCode;
    }
    if (mFailure != JoinFailure::None) {
        mProgress = JoinProgress::Failed;
        return mFailure;
    }
    mOrder = mCode.endpoints;
    std::stable_sort(mOrder.begin(), mOrder.end(),
                     [](const DirectEndpoint& a, const DirectEndpoint& b) { return a.priority < b.priority; });
    mNext = 0;
    mProgress = JoinProgress::Connecting;
    if (!tryNext(nowMs)) {
        mProgress = JoinProgress::Failed;
        if (mFailure == JoinFailure::None) mFailure = JoinFailure::Unreachable;
    }
    return mFailure;
}

bool GuestJoin::tryNext(std::uint64_t nowMs)
{
    if (mTransport) mTransport->close();
    mTransport.reset();
    while (mNext < mOrder.size()) {
        const DirectEndpoint endpoint = mOrder[mNext++];
        std::unique_ptr<ITransport> inner = mFactory ? mFactory() : nullptr;
        if (!inner) {
            mFailure = JoinFailure::NoTransport;
            return false;
        }
        auto gated = std::make_unique<GatedTransport>(std::move(inner), GatedTransport::Role::Guest,
                                                      joinCredentialsFromCode(mCode));
        gated->requireCompatibility(mLocal);
        if (!gated->connect({endpointAddressText(endpoint), endpoint.port})) continue;
        mTransport = std::move(gated);
        mDeadlineMs = nowMs + kAttemptTimeoutMs;
        return true;
    }
    return false;
}

JoinProgress GuestJoin::poll(std::uint64_t nowMs, TransportEvent& event, bool& hasEvent)
{
    hasEvent = false;
    if (mProgress != JoinProgress::Connecting && mProgress != JoinProgress::Connected) return mProgress;
    if (!mTransport) return mProgress;

    auto fail = [this](JoinFailure failure) {
        mFailure = failure;
        mProgress = JoinProgress::Failed;
        // Drop the link but keep the compatibility verdict for the UI.
        if (mTransport) mTransport->disconnectPeer(false);
        return mProgress;
    };
    auto nextOrFail = [&]() {
        if (tryNext(nowMs)) return mProgress;
        // Every endpoint failed: report the most telling reason.
        return fail(mFailure != JoinFailure::None ? mFailure : JoinFailure::Unreachable);
    };

    TransportEvent incoming;
    while (mTransport->poll(incoming)) {
        if (mProgress == JoinProgress::Connected) {
            if (incoming.type == TransportEventType::Disconnected || incoming.type == TransportEventType::Error)
                return fail(JoinFailure::Lost);
            event = std::move(incoming);
            hasEvent = true;
            return mProgress;
        }
        switch (incoming.type) {
        case TransportEventType::Connected:
            mProgress = JoinProgress::Connected;
            event = std::move(incoming);
            hasEvent = true;
            return mProgress;
        case TransportEventType::Message:
            break; // nothing reaches the session before Connected
        case TransportEventType::Disconnected:
        case TransportEventType::Error:
            if (incoming.detail == kRoomFullReason) return fail(JoinFailure::RoomFull);
            if (incoming.detail == joinErrorString(JoinError::Refused)) return fail(JoinFailure::Refused);
            if (incoming.detail.rfind(joinErrorString(JoinError::Incompatible), 0) == 0)
                return fail(JoinFailure::Incompatible);
            return nextOrFail();
        }
    }
    if (mProgress == JoinProgress::Connecting && nowMs >= mDeadlineMs) return nextOrFail();
    return mProgress;
}

CompatibilityField GuestJoin::incompatibility() const
{
    return mTransport && mTransport->compatibility() ? mTransport->compatibility()->mismatch()
                                                     : CompatibilityField::None;
}

const CompatibilityInfo* GuestJoin::host() const
{
    if (!mTransport || !mTransport->compatibility()) return nullptr;
    const CompatibilityState state = mTransport->compatibility()->state();
    return state == CompatibilityState::Compatible || state == CompatibilityState::Incompatible
        ? &mTransport->compatibility()->peer()
        : nullptr;
}

void GuestJoin::cancel()
{
    if (mTransport) mTransport->close();
    mTransport.reset();
    secureWipe(mCode.joinSecret.data(), mCode.joinSecret.size());
    mCode = ConnectionCode{};
    mOrder.clear();
    mNext = 0;
    mProgress = JoinProgress::Idle;
}

} // namespace nectar::net
