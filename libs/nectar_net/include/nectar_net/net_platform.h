#pragma once

#include "nectar_net/port_mapping.h"

#include <memory>
#include <vector>

namespace nectar::net {

// Operating-system queries needed to publish a host. Nothing here contacts
// the Internet: the routing table, the local interfaces and one UDP socket
// to the gateway on the LAN.

// IPv4 next hop of the default route. False when the platform hides it:
// Android 10+ denies /proc/net/route to apps, so there the Java side must
// supply the gateway (ConnectivityManager/LinkProperties).
bool findDefaultGateway(Ipv4Address& gateway);

// Android: the app's Java side reads the default route from
// ConnectivityManager and hands it over here; findDefaultGateway prefers it.
void setGatewayHint(const Ipv4Address& gateway);

struct LocalAddresses {
    std::vector<Ipv4Address> ipv4;
    std::vector<Ipv6Address> ipv6;
};

// Unicast addresses of interfaces that are up, loopback excluded.
LocalAddresses localAddresses();

// UDP socket connected to gateway:port, so the kernel drops datagrams from
// anyone else. Null when the socket cannot be created.
std::unique_ptr<IDatagramChannel> openGatewayChannel(const Ipv4Address& gateway,
                                                     std::uint16_t port = kGatewayMappingPort);

} // namespace nectar::net
