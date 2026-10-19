#include "nectar_net/port_publisher.h"

#include "nectar_net/secure_random.h"

#include <algorithm>

namespace nectar::net {

namespace {

constexpr std::uint8_t kPriorityIpv6 = 10;
constexpr std::uint8_t kPriorityPublicIpv4 = 20;
constexpr std::uint8_t kPriorityLanIpv4 = 30;
constexpr std::size_t kMaxIpv6Endpoints = 2;
// A failed renewal is retried this soon instead of at half-life.
constexpr std::uint64_t kRenewRetryMs = 30U * 1000U;

bool publicIpv4(const Ipv4Address& address) { return classifyIpv4(address) == AddressScope::Global; }

} // namespace

const char* reachabilityString(Reachability reachability)
{
    switch (reachability) {
    case Reachability::Public:             return "public";
    case Reachability::Ipv6Only:           return "ipv6_only";
    case Reachability::BehindCgnat:        return "behind_cgnat";
    case Reachability::NoAutomaticMapping: return "no_automatic_mapping";
    }
    return "invalid";
}

PublisherBackends PublisherBackends::system()
{
    PublisherBackends backends;
    backends.findGateway = findDefaultGateway;
    backends.openChannel = [](const Ipv4Address& gateway) { return openGatewayChannel(gateway); };
    backends.openUpnp = createUpnpClient;
    backends.listAddresses = localAddresses;
    backends.randomBytes = secureRandomBytes;
    return backends;
}

PortPublisher::PortPublisher(PublisherBackends backends) : mBackends(std::move(backends)) {}

PortPublisher::~PortPublisher() { withdraw(); }

bool PortPublisher::mapIpv4(std::uint16_t localPort, std::uint32_t lifetimeSeconds, PublishResult& result)
{
    mRequest = MappingRequest{};
    mRequest.internalPort = localPort;
    mRequest.suggestedExternalPort = localPort;
    mRequest.lifetimeSeconds = lifetimeSeconds;
    if (mBackends.randomBytes && !mBackends.randomBytes(mRequest.nonce.data(), mRequest.nonce.size())) return false;

    Ipv4Address gateway{};
    if (mBackends.findGateway && mBackends.findGateway(gateway) && mBackends.openChannel) {
        mChannel = mBackends.openChannel(gateway);
    }
    if (mChannel) {
        if (classifyIpv4(mChannel->localAddress()) == AddressScope::Private) mLanAddress = mChannel->localAddress();
        result.pcpError = pcpMap(*mChannel, mRequest, mMapping, mBackends.retry);
        if (result.pcpError == MappingError::None) {
            mMethod = MappingMethod::Pcp;
            return true;
        }
        // A NAT-PMP-only router answers PCP with "unsupported version"; a
        // silent one will not speak NAT-PMP either, so skip that wait.
        if (result.pcpError == MappingError::UnsupportedVersion) {
            result.natPmpError = natPmpMap(*mChannel, mRequest, mMapping, mBackends.retry);
            if (result.natPmpError == MappingError::None) {
                mMethod = MappingMethod::NatPmp;
                return true;
            }
        } else {
            result.natPmpError = MappingError::Timeout;
        }
    } else {
        result.pcpError = result.natPmpError = MappingError::NoGateway;
    }

    mUpnp = mBackends.openUpnp ? mBackends.openUpnp() : nullptr;
    if (!mUpnp) {
        result.upnpError = MappingError::NoGateway;
        return false;
    }
    result.upnpError = mUpnp->discover(mBackends.upnpDiscoveryMs);
    if (result.upnpError != MappingError::None) return false;
    PortMapping mapping;
    mapping.method = MappingMethod::Upnp;
    mapping.internalPort = localPort;
    mapping.externalPort = localPort;
    result.upnpError =
        mUpnp->addMapping(localPort, localPort, lifetimeSeconds, mapping.externalAddress, mapping.lifetimeSeconds);
    if (result.upnpError != MappingError::None) return false;
    mMapping = mapping;
    mMethod = MappingMethod::Upnp;
    return true;
}

std::vector<DirectEndpoint> PortPublisher::endpointsFor(const PublishResult& result, bool includeLan) const
{
    std::vector<DirectEndpoint> endpoints;
    for (std::size_t i = 0; i < mIpv6.size() && i < kMaxIpv6Endpoints; ++i) {
        DirectEndpoint v6;
        v6.family = DirectEndpoint::Family::Ipv6;
        std::copy(mIpv6[i].begin(), mIpv6[i].end(), v6.address.begin());
        v6.port = mRequest.internalPort; // no NAT on IPv6: the local port is the public one
        v6.priority = kPriorityIpv6;
        endpoints.push_back(v6);
    }
    if (result.reachability == Reachability::Public) {
        DirectEndpoint v4;
        std::copy(mMapping.externalAddress.begin(), mMapping.externalAddress.end(), v4.address.begin());
        v4.port = mMapping.externalPort;
        v4.priority = kPriorityPublicIpv4;
        endpoints.push_back(v4);
    }
    if (includeLan && classifyIpv4(mLanAddress) == AddressScope::Private
        && endpoints.size() < ConnectionCode::kMaxEndpoints) {
        DirectEndpoint lan;
        std::copy(mLanAddress.begin(), mLanAddress.end(), lan.address.begin());
        lan.port = mRequest.internalPort;
        lan.priority = kPriorityLanIpv4;
        endpoints.push_back(lan);
    }
    return endpoints;
}

PublishResult PortPublisher::publish(std::uint16_t localPort, std::uint32_t lifetimeSeconds, bool includeLan)
{
    withdraw();
    PublishResult result;
    mIncludeLan = includeLan;
    mLanAddress = Ipv4Address{};
    mIpv6.clear();
    if (mBackends.listAddresses) {
        const LocalAddresses local = mBackends.listAddresses();
        for (const Ipv6Address& address : local.ipv6)
            if (classifyIpv6(address) == AddressScope::Global) mIpv6.push_back(address);
        for (const Ipv4Address& address : local.ipv4) {
            if (classifyIpv4(address) == AddressScope::Private) {
                mLanAddress = address;
                break;
            }
        }
    }

    const bool mapped = localPort != 0 && mapIpv4(localPort, lifetimeSeconds, result);
    mRequest.internalPort = localPort;
    result.method = mMethod;
    result.mapping = mMapping;
    if (mapped && publicIpv4(mMapping.externalAddress)) {
        result.reachability = Reachability::Public;
    } else {
        // A mapping whose "external" side is still private (double NAT or
        // CGNAT) is useless from the Internet: remove it, keep the diagnosis.
        if (mapped) withdraw();
        result.reachability = !mIpv6.empty() ? Reachability::Ipv6Only
                            : mapped         ? Reachability::BehindCgnat
                                             : Reachability::NoAutomaticMapping;
    }
    result.endpoints = endpointsFor(result, includeLan);
    return result;
}

bool PortPublisher::maintain(std::uint64_t nowMs, PublishResult& current)
{
    if (mMethod == MappingMethod::None || mMapping.lifetimeSeconds == 0) return false;
    if (mRenewAtMs == 0) {
        mRenewAtMs = nowMs + static_cast<std::uint64_t>(mMapping.lifetimeSeconds) * 500U;
        return false;
    }
    if (nowMs < mRenewAtMs) return false;

    PortMapping renewed;
    MappingError error = MappingError::NoGateway;
    if (mMethod == MappingMethod::Pcp && mChannel) {
        error = pcpMap(*mChannel, mRequest, renewed, mBackends.retry);
    } else if (mMethod == MappingMethod::NatPmp && mChannel) {
        error = natPmpMap(*mChannel, mRequest, renewed, mBackends.retry);
    } else if (mMethod == MappingMethod::Upnp && mUpnp) {
        renewed.method = MappingMethod::Upnp;
        renewed.internalPort = mMapping.internalPort;
        renewed.externalPort = mMapping.externalPort;
        error = mUpnp->addMapping(mMapping.internalPort, mMapping.externalPort, mRequest.lifetimeSeconds,
                                  renewed.externalAddress, renewed.lifetimeSeconds);
    }
    if (error != MappingError::None) {
        mRenewAtMs = nowMs + kRenewRetryMs;
        return false;
    }
    const bool changed =
        renewed.externalAddress != mMapping.externalAddress || renewed.externalPort != mMapping.externalPort;
    mMapping = renewed;
    // Permanent UPnP leases need no renewal.
    mRenewAtMs = mMapping.lifetimeSeconds == 0 ? UINT64_MAX
                                              : nowMs + static_cast<std::uint64_t>(mMapping.lifetimeSeconds) * 500U;
    if (!changed) return false;
    current.mapping = mMapping;
    current.reachability = publicIpv4(mMapping.externalAddress) ? Reachability::Public : Reachability::BehindCgnat;
    current.endpoints = endpointsFor(current, mIncludeLan);
    return true;
}

void PortPublisher::withdraw()
{
    if (mMethod == MappingMethod::Pcp || mMethod == MappingMethod::NatPmp) {
        if (mChannel) {
            MappingRequest removal = mRequest;
            removal.lifetimeSeconds = 0;
            PortMapping ignored;
            // One quick attempt: an expired lease cleans itself up anyway.
            const RetryPolicy once{mBackends.retry.initialTimeoutMs, 2};
            if (mMethod == MappingMethod::Pcp)
                pcpMap(*mChannel, removal, ignored, once);
            else
                natPmpMap(*mChannel, removal, ignored, once);
        }
    } else if (mMethod == MappingMethod::Upnp && mUpnp) {
        mUpnp->deleteMapping(mMapping.externalPort);
    }
    mMethod = MappingMethod::None;
    mMapping = PortMapping{};
    mChannel.reset();
    mUpnp.reset();
    mRenewAtMs = 0;
}

} // namespace nectar::net
