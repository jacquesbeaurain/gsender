#include "gs/transport/network_list.hpp"

#include <boost/asio.hpp>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#endif

#include "wide_text.hpp"

namespace gs::transport {

#ifdef _WIN32
std::vector<remote::InterfaceAddress> listInterfaceAddresses() {
    std::vector<remote::InterfaceAddress> out;
    ULONG size = 16 * 1024;
    std::vector<unsigned char> buffer;
    ULONG result = ERROR_BUFFER_OVERFLOW;
    for (int attempt = 0; attempt < 3 && result == ERROR_BUFFER_OVERFLOW; ++attempt) {
        buffer.resize(size);
        result = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                                      nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()), &size);
    }
    if (result != NO_ERROR) {
        return out;
    }
    for (auto* adapter = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()); adapter; adapter = adapter->Next) {
        if (adapter->OperStatus != IfOperStatusUp) {
            continue;
        }
        const std::string name = narrow(adapter->FriendlyName);
        const bool loopback = adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK;
        for (auto* unicast = adapter->FirstUnicastAddress; unicast; unicast = unicast->Next) {
            const sockaddr* address = unicast->Address.lpSockaddr;
            if (!address || address->sa_family != AF_INET) {
                continue;
            }
            char text[INET_ADDRSTRLEN] = {};
            const auto* in = reinterpret_cast<const sockaddr_in*>(address);
            if (inet_ntop(AF_INET, &in->sin_addr, text, sizeof text)) {
                out.push_back({name, text, loopback});
            }
        }
    }
    return out;
}
#else
std::vector<remote::InterfaceAddress> listInterfaceAddresses() {
    std::vector<remote::InterfaceAddress> out;
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) {
        return out;
    }
    for (ifaddrs* entry = list; entry; entry = entry->ifa_next) {
        if (!entry->ifa_addr || entry->ifa_addr->sa_family != AF_INET || !(entry->ifa_flags & IFF_UP)) {
            continue;
        }
        char text[INET_ADDRSTRLEN] = {};
        const auto* in = reinterpret_cast<const sockaddr_in*>(entry->ifa_addr);
        if (inet_ntop(AF_INET, &in->sin_addr, text, sizeof text)) {
            out.push_back({entry->ifa_name ? entry->ifa_name : "", text, (entry->ifa_flags & IFF_LOOPBACK) != 0});
        }
    }
    freeifaddrs(list);
    return out;
}
#endif

std::optional<std::string> defaultRouteAddress() {
    namespace asio = boost::asio;
    asio::io_context io;
    asio::ip::udp::socket socket(io);
    boost::system::error_code ec;
    socket.open(asio::ip::udp::v4(), ec);
    if (ec) {
        return std::nullopt;
    }
    socket.connect({asio::ip::make_address_v4("8.8.8.8"), 53}, ec);
    if (ec) {
        return std::nullopt;
    }
    const auto local = socket.local_endpoint(ec);
    if (ec || local.address().is_unspecified()) {
        return std::nullopt;
    }
    return local.address().to_string();
}

std::vector<remote::NetworkAddress> listNetworkAddresses() {
    return remote::rankAddresses(listInterfaceAddresses(), defaultRouteAddress());
}

}  // namespace gs::transport
