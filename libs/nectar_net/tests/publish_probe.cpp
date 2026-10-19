// Manual check against a real router: asks it to forward one UDP port with
// PCP, NAT-PMP or UPnP, prints what worked, then removes the mapping.
//
//   nectar_net_publish_probe [port] [--show-addresses]
//
// Addresses are hidden unless asked for, so the output can be shared.
#include "nectar_net/port_publisher.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

using namespace nectar::net;

namespace {

void printEndpoint(const DirectEndpoint& endpoint, bool show)
{
    const char* kind = endpoint.priority == 10 ? "IPv6 global" : endpoint.priority == 20 ? "IPv4 público" : "IPv4 LAN";
    if (!show) {
        std::printf("  - %s, puerto %u\n", kind, endpoint.port);
        return;
    }
    if (endpoint.family == DirectEndpoint::Family::Ipv4) {
        std::printf("  - %s %u.%u.%u.%u:%u\n", kind, endpoint.address[0], endpoint.address[1], endpoint.address[2],
                    endpoint.address[3], endpoint.port);
    } else {
        std::printf("  - %s [", kind);
        for (int i = 0; i < 16; i += 2)
            std::printf("%s%x", i ? ":" : "", (endpoint.address[static_cast<std::size_t>(i)] << 8)
                                                   | endpoint.address[static_cast<std::size_t>(i) + 1]);
        std::printf("]:%u\n", endpoint.port);
    }
}

} // namespace

int main(int argc, char** argv)
{
    std::uint16_t port = 39990;
    bool show = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--show-addresses") == 0)
            show = true;
        else
            port = static_cast<std::uint16_t>(std::atoi(argv[i]));
    }
    std::printf("Pidiendo al router que abra el puerto UDP %u (PCP, NAT-PMP, UPnP)...\n", port);
    PortPublisher publisher;
    const PublishResult result = publisher.publish(port, 600);
    std::printf("Método: %s\n", mappingMethodString(result.method));
    std::printf("Resultado: %s\n", reachabilityString(result.reachability));
    std::printf("PCP: %s, NAT-PMP: %s, UPnP: %s\n", mappingErrorString(result.pcpError),
                mappingErrorString(result.natPmpError), mappingErrorString(result.upnpError));
    std::printf("Endpoints para el código:\n");
    for (const DirectEndpoint& endpoint : result.endpoints) printEndpoint(endpoint, show);
    std::printf("Quitando el mapeo del router...\n");
    publisher.withdraw();
    std::printf("Hecho.\n");
    return 0;
}
