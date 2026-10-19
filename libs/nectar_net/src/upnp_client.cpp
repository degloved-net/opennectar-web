#include "nectar_net/port_publisher.h"

#ifdef NECTAR_NET_HAS_UPNP
#include <miniupnpc.h>
#include <upnpcommands.h>

#include <cstdio>
#include <cstring>
#include <string>
#endif

namespace nectar::net {

#ifdef NECTAR_NET_HAS_UPNP

namespace {

constexpr int kOnlyPermanentLeasesSupported = 725;
constexpr int kConflictInMappingEntry = 718;
constexpr int kActionNotAuthorized = 606;

bool parseIpv4(const char* text, Ipv4Address& address)
{
    unsigned a = 0, b = 0, c = 0, d = 0;
    char trailing = 0;
    if (std::sscanf(text, "%3u.%3u.%3u.%3u%c", &a, &b, &c, &d, &trailing) != 4 || a > 255 || b > 255 || c > 255
        || d > 255) {
        return false;
    }
    address = {static_cast<std::uint8_t>(a), static_cast<std::uint8_t>(b), static_cast<std::uint8_t>(c),
               static_cast<std::uint8_t>(d)};
    return true;
}

class MiniUpnpClient final : public IUpnpClient {
public:
    ~MiniUpnpClient() override
    {
        if (mValid) FreeUPNPUrls(&mUrls);
    }

    MappingError discover(std::uint32_t timeoutMs) override
    {
        if (mValid) {
            FreeUPNPUrls(&mUrls);
            mValid = false;
        }
        int error = 0;
        // One search, one wait: upnpDiscover() tries several device and
        // service types in turn and waits timeoutMs for each, which multiplies
        // the delay when no router answers. IGD:2 devices answer a search for
        // IGD:1 too (UDA: a device answers searches for lower versions).
        static const char* const kTypes[] = { "urn:schemas-upnp-org:device:InternetGatewayDevice:1", nullptr };
        UPNPDev* devices =
            upnpDiscoverDevices(kTypes, static_cast<int>(timeoutMs), nullptr, nullptr, 0, 0, 2, &error, 0);
        if (devices == nullptr) return MappingError::Timeout;
        char wan[64] = {};
        const int found = UPNP_GetValidIGD(devices, &mUrls, &mData, mLan, sizeof(mLan), wan, sizeof(wan));
        freeUPNPDevlist(devices);
        switch (found) {
        case UPNP_CONNECTED_IGD:
        case UPNP_PRIVATEIP_IGD: // still mapped; the caller spots the private address
            mValid = true;
            return MappingError::None;
        case UPNP_DISCONNECTED_IGD:
            FreeUPNPUrls(&mUrls);
            return MappingError::NetworkFailure;
        case UPNP_UNKNOWN_DEVICE:
            FreeUPNPUrls(&mUrls);
            return MappingError::NoGateway;
        default:
            return MappingError::NoGateway;
        }
    }

    MappingError addMapping(std::uint16_t internalPort, std::uint16_t externalPort, std::uint32_t leaseSeconds,
                            Ipv4Address& externalAddress, std::uint32_t& grantedLeaseSeconds) override
    {
        if (!mValid) return MappingError::NoGateway;
        const std::string external = std::to_string(externalPort);
        const std::string internal = std::to_string(internalPort);
        std::string lease = std::to_string(leaseSeconds);
        int result = UPNP_AddPortMapping(mUrls.controlURL, mData.first.servicetype, external.c_str(),
                                         internal.c_str(), mLan, "Open Nectar", "UDP", nullptr, lease.c_str());
        if (result == kOnlyPermanentLeasesSupported) {
            lease = "0";
            result = UPNP_AddPortMapping(mUrls.controlURL, mData.first.servicetype, external.c_str(),
                                         internal.c_str(), mLan, "Open Nectar", "UDP", nullptr, lease.c_str());
        }
        if (result == kActionNotAuthorized) return MappingError::NotAuthorized;
        if (result == kConflictInMappingEntry) return MappingError::Refused;
        if (result != UPNPCOMMAND_SUCCESS) return MappingError::Refused;
        grantedLeaseSeconds = lease == "0" ? 0 : leaseSeconds;

        char address[40] = {};
        if (UPNP_GetExternalIPAddress(mUrls.controlURL, mData.first.servicetype, address) != UPNPCOMMAND_SUCCESS
            || !parseIpv4(address, externalAddress)) {
            deleteMapping(externalPort);
            return MappingError::NetworkFailure;
        }
        return MappingError::None;
    }

    MappingError deleteMapping(std::uint16_t externalPort) override
    {
        if (!mValid) return MappingError::NoGateway;
        const std::string external = std::to_string(externalPort);
        return UPNP_DeletePortMapping(mUrls.controlURL, mData.first.servicetype, external.c_str(), "UDP", nullptr)
                == UPNPCOMMAND_SUCCESS
            ? MappingError::None
            : MappingError::Refused;
    }

private:
    UPNPUrls mUrls{};
    IGDdatas mData{};
    char mLan[64] = {};
    bool mValid = false;
};

} // namespace

std::unique_ptr<IUpnpClient> createUpnpClient() { return std::make_unique<MiniUpnpClient>(); }

#else

std::unique_ptr<IUpnpClient> createUpnpClient() { return nullptr; }

#endif

} // namespace nectar::net
