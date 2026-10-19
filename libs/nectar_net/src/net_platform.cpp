#include "nectar_net/net_platform.h"

#include <atomic>
#include <cstring>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace nectar::net {

namespace {

constexpr std::size_t kMaxDatagramBytes = 1500;

std::atomic<std::uint32_t> sGatewayHint{0};

bool takeGatewayHint(Ipv4Address& gateway)
{
    const std::uint32_t v = sGatewayHint.load();
    if (v == 0) return false;
    gateway = {static_cast<std::uint8_t>(v >> 24), static_cast<std::uint8_t>(v >> 16),
               static_cast<std::uint8_t>(v >> 8), static_cast<std::uint8_t>(v)};
    return true;
}

#ifdef _WIN32
struct WinsockUser {
    bool ready = false;
    WinsockUser()
    {
        WSADATA data;
        ready = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }
    ~WinsockUser()
    {
        if (ready) WSACleanup();
    }
};
using NativeSocket = SOCKET;
const NativeSocket kInvalidSocket = INVALID_SOCKET;
void closeSocket(NativeSocket s) { closesocket(s); }
#else
using NativeSocket = int;
const NativeSocket kInvalidSocket = -1;
void closeSocket(NativeSocket s) { close(s); }
#endif

class UdpGatewayChannel final : public IDatagramChannel {
public:
    explicit UdpGatewayChannel(NativeSocket socket, const Ipv4Address& local) : mSocket(socket), mLocal(local) {}
    ~UdpGatewayChannel() override { closeSocket(mSocket); }

    bool send(const std::uint8_t* data, std::size_t size) override
    {
#ifdef _WIN32
        return ::send(mSocket, reinterpret_cast<const char*>(data), static_cast<int>(size), 0)
            == static_cast<int>(size);
#else
        return ::send(mSocket, data, size, 0) == static_cast<ssize_t>(size);
#endif
    }

    bool receive(std::vector<std::uint8_t>& output, std::uint32_t timeoutMs) override
    {
        output.resize(kMaxDatagramBytes);
#ifdef _WIN32
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(mSocket, &readable);
        timeval wait{static_cast<long>(timeoutMs / 1000), static_cast<long>((timeoutMs % 1000) * 1000)};
        if (select(0, &readable, nullptr, nullptr, &wait) != 1) return false;
        const int got = recv(mSocket, reinterpret_cast<char*>(output.data()), static_cast<int>(output.size()), 0);
#else
        pollfd entry{mSocket, POLLIN, 0};
        if (poll(&entry, 1, static_cast<int>(timeoutMs)) != 1) return false;
        const ssize_t got = recv(mSocket, output.data(), output.size(), MSG_DONTWAIT);
#endif
        // An ICMP "port unreachable" (no PCP/NAT-PMP service) surfaces as an
        // error here; treating it as no answer lets the caller move on.
        if (got <= 0) return false;
        output.resize(static_cast<std::size_t>(got));
        return true;
    }

    Ipv4Address localAddress() const override { return mLocal; }

private:
#ifdef _WIN32
    WinsockUser mWinsock;
#endif
    NativeSocket mSocket;
    Ipv4Address mLocal;
};

} // namespace

#ifdef _WIN32

bool findDefaultGateway(Ipv4Address& gateway)
{
    if (takeGatewayHint(gateway)) return true;
    ULONG size = 0;
    if (GetIpForwardTable(nullptr, &size, FALSE) != ERROR_INSUFFICIENT_BUFFER || size == 0) return false;
    std::vector<std::uint8_t> buffer(size);
    auto* table = reinterpret_cast<MIB_IPFORWARDTABLE*>(buffer.data());
    if (GetIpForwardTable(table, &size, TRUE) != NO_ERROR) return false;
    bool found = false;
    DWORD bestMetric = 0;
    for (DWORD i = 0; i < table->dwNumEntries; ++i) {
        const MIB_IPFORWARDROW& row = table->table[i];
        if (row.dwForwardDest != 0 || row.dwForwardMask != 0 || row.dwForwardNextHop == 0) continue;
        if (found && row.dwForwardMetric1 >= bestMetric) continue;
        std::memcpy(gateway.data(), &row.dwForwardNextHop, 4); // already network order
        bestMetric = row.dwForwardMetric1;
        found = true;
    }
    return found;
}

LocalAddresses localAddresses()
{
    LocalAddresses result;
    ULONG size = 16 * 1024;
    std::vector<std::uint8_t> buffer;
    const ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    ULONG status = ERROR_BUFFER_OVERFLOW;
    for (int attempt = 0; attempt < 3 && status == ERROR_BUFFER_OVERFLOW; ++attempt) {
        buffer.resize(size);
        status = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr,
                                      reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()), &size);
    }
    if (status != NO_ERROR) return result;
    for (auto* adapter = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()); adapter != nullptr;
         adapter = adapter->Next) {
        if (adapter->OperStatus != IfOperStatusUp || adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        for (auto* unicast = adapter->FirstUnicastAddress; unicast != nullptr; unicast = unicast->Next) {
            const sockaddr* address = unicast->Address.lpSockaddr;
            if (address->sa_family == AF_INET) {
                Ipv4Address v4;
                std::memcpy(v4.data(), &reinterpret_cast<const sockaddr_in*>(address)->sin_addr, 4);
                result.ipv4.push_back(v4);
            } else if (address->sa_family == AF_INET6 && unicast->DadState == IpDadStatePreferred) {
                Ipv6Address v6;
                std::memcpy(v6.data(), &reinterpret_cast<const sockaddr_in6*>(address)->sin6_addr, 16);
                result.ipv6.push_back(v6);
            }
        }
    }
    return result;
}

#else

bool findDefaultGateway(Ipv4Address& gateway)
{
    if (takeGatewayHint(gateway)) return true;
    std::FILE* routes = std::fopen("/proc/net/route", "r");
    if (routes == nullptr) return false;
    char line[256];
    bool found = false;
    unsigned long bestMetric = 0;
    if (std::fgets(line, sizeof(line), routes) == nullptr) { // header
        std::fclose(routes);
        return false;
    }
    while (std::fgets(line, sizeof(line), routes) != nullptr) {
        char iface[64];
        unsigned long destination = 0, nextHop = 0, flagsValue = 0, refs = 0, use = 0, metric = 0, mask = 0;
        if (std::sscanf(line, "%63s %lx %lx %lx %lu %lu %lu %lx", iface, &destination, &nextHop, &flagsValue, &refs,
                        &use, &metric, &mask)
            != 8) {
            continue;
        }
        // RTF_UP | RTF_GATEWAY on the default route.
        if (destination != 0 || mask != 0 || (flagsValue & 0x3) != 0x3 || nextHop == 0) continue;
        if (found && metric >= bestMetric) continue;
        // The kernel prints the address in host byte order (little endian).
        const std::uint32_t value = static_cast<std::uint32_t>(nextHop);
        gateway = {static_cast<std::uint8_t>(value), static_cast<std::uint8_t>(value >> 8),
                   static_cast<std::uint8_t>(value >> 16), static_cast<std::uint8_t>(value >> 24)};
        bestMetric = metric;
        found = true;
    }
    std::fclose(routes);
    return found;
}

LocalAddresses localAddresses()
{
    LocalAddresses result;
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) return result;
    for (ifaddrs* entry = list; entry != nullptr; entry = entry->ifa_next) {
        if (entry->ifa_addr == nullptr || (entry->ifa_flags & IFF_UP) == 0 || (entry->ifa_flags & IFF_LOOPBACK) != 0)
            continue;
        if (entry->ifa_addr->sa_family == AF_INET) {
            Ipv4Address v4;
            std::memcpy(v4.data(), &reinterpret_cast<const sockaddr_in*>(entry->ifa_addr)->sin_addr, 4);
            result.ipv4.push_back(v4);
        } else if (entry->ifa_addr->sa_family == AF_INET6) {
            Ipv6Address v6;
            std::memcpy(v6.data(), &reinterpret_cast<const sockaddr_in6*>(entry->ifa_addr)->sin6_addr, 16);
            result.ipv6.push_back(v6);
        }
    }
    freeifaddrs(list);
    return result;
}

#endif

void setGatewayHint(const Ipv4Address& gateway)
{
    sGatewayHint.store((static_cast<std::uint32_t>(gateway[0]) << 24) | (static_cast<std::uint32_t>(gateway[1]) << 16)
                       | (static_cast<std::uint32_t>(gateway[2]) << 8) | gateway[3]);
}

std::unique_ptr<IDatagramChannel> openGatewayChannel(const Ipv4Address& gateway, std::uint16_t port)
{
#ifdef _WIN32
    WinsockUser winsock;
    if (!winsock.ready) return nullptr;
#endif
    NativeSocket s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == kInvalidSocket) return nullptr;
#ifndef _WIN32
    fcntl(s, F_SETFD, FD_CLOEXEC);
#endif
    sockaddr_in remote{};
    remote.sin_family = AF_INET;
    remote.sin_port = htons(port);
    std::memcpy(&remote.sin_addr, gateway.data(), 4);
    if (connect(s, reinterpret_cast<const sockaddr*>(&remote), sizeof(remote)) != 0) {
        closeSocket(s);
        return nullptr;
    }
    sockaddr_in local{};
#ifdef _WIN32
    int length = sizeof(local);
#else
    socklen_t length = sizeof(local);
#endif
    Ipv4Address localAddress{};
    if (getsockname(s, reinterpret_cast<sockaddr*>(&local), &length) == 0)
        std::memcpy(localAddress.data(), &local.sin_addr, 4);
    return std::make_unique<UdpGatewayChannel>(s, localAddress);
}

} // namespace nectar::net
