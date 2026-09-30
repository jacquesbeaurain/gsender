#include "gs/remote/network.hpp"

#include <boost/json.hpp>
#include <boost/regex.hpp>

#include <algorithm>
#include <map>

namespace gs::remote {
namespace {

// Adapter names that never carry traffic another device on the shop network
// can reach: hypervisors, container bridges, VPN/overlay networks and
// capture drivers.
const boost::regex& virtualName() {
    static const boost::regex re(
        "vmware|virtualbox|vbox|hyper-?v|vethernet|wsl|docker|tailscale|zerotier|utun|npcap|loopback|bridge|veth|tun\\d|tap\\d",
        boost::regex::icase);
    return re;
}

bool matches(std::string_view text, const char* pattern) {
    const boost::regex re(pattern, boost::regex::icase);
    return boost::regex_search(text.begin(), text.end(), re);
}

int rank(AddressKind kind) {
    return static_cast<int>(kind);
}

// Self-assigned addresses handed out when DHCP fails: the adapter is up but
// there is no working network behind it.
bool isApipa(std::string_view address) {
    return address.starts_with("169.254.");
}

bool isPrivate(std::string_view address) {
    return matches(address, "^192\\.168\\.") || matches(address, "^10\\.") ||
           matches(address, "^172\\.(1[6-9]|2\\d|3[01])\\.");
}

// pickFallback(): the usable address with the best kind, preferring private
// ranges (192.168 before 10 before 172.16-31) over public ones.
std::optional<std::string> pickFallback(const std::vector<NetworkAddress>& addresses) {
    const NetworkAddress* best = nullptr;
    int bestScore = 0;
    for (const NetworkAddress& entry : addresses) {
        if (!entry.usable) {
            continue;
        }
        int score = rank(entry.kind);
        if (!isPrivate(entry.address)) {
            score += 10;
        } else if (entry.address.starts_with("192.168.")) {
            score += 0;
        } else if (entry.address.starts_with("10.")) {
            score += 1;
        } else {
            score += 2;
        }
        // reduce() keeps the earlier entry on a tie.
        if (!best || score < bestScore) {
            best = &entry;
            bestScore = score;
        }
    }
    return best ? std::optional<std::string>(best->address) : std::nullopt;
}

}  // namespace

AddressKind classify(std::string_view iface, bool internal) {
    if (internal) {
        return AddressKind::Loopback;
    }
    if (boost::regex_search(iface.begin(), iface.end(), virtualName())) {
        return AddressKind::Virtual;
    }
    if (matches(iface, "wi-?fi|wlan|wireless|airport")) {
        return AddressKind::Wifi;
    }
    if (matches(iface, "ethernet|^en\\d|^eth\\d|lan")) {
        return AddressKind::Ethernet;
    }
    return AddressKind::Unknown;
}

std::string kindLabel(AddressKind kind) {
    switch (kind) {
        case AddressKind::Wifi: return "Wi-Fi";
        case AddressKind::Ethernet: return "Ethernet";
        case AddressKind::Virtual: return "Virtual adapter";
        case AddressKind::Loopback: return "This computer only";
        case AddressKind::Unknown: break;
    }
    return "Network";
}

std::vector<NetworkAddress> rankAddresses(const std::vector<InterfaceAddress>& interfaces,
                                          const std::optional<std::string>& defaultRouteAddress) {
    // collectAddresses(): insertion order of first appearance, replaced by a
    // better-ranked interface for the same address.
    std::vector<NetworkAddress> addresses;
    std::map<std::string, std::size_t> byAddress;
    for (const InterfaceAddress& entry : interfaces) {
        const AddressKind kind = classify(entry.iface, entry.internal);
        NetworkAddress item{entry.address,
                            entry.iface,
                            kind,
                            kindLabel(kind),
                            kind != AddressKind::Loopback && kind != AddressKind::Virtual && !isApipa(entry.address),
                            false};
        const auto it = byAddress.find(entry.address);
        if (it != byAddress.end()) {
            if (rank(addresses[it->second].kind) <= rank(kind)) {
                continue;
            }
            addresses[it->second] = std::move(item);
            continue;
        }
        byAddress.emplace(entry.address, addresses.size());
        addresses.push_back(std::move(item));
    }

    std::optional<std::string> recommended;
    if (defaultRouteAddress && std::any_of(addresses.begin(), addresses.end(), [&](const NetworkAddress& a) {
            return a.address == *defaultRouteAddress && a.usable;
        })) {
        recommended = defaultRouteAddress;
    } else {
        recommended = pickFallback(addresses);
    }
    for (NetworkAddress& entry : addresses) {
        entry.recommended = recommended && entry.address == *recommended;
    }

    std::stable_sort(addresses.begin(), addresses.end(), [](const NetworkAddress& a, const NetworkAddress& b) {
        if (a.recommended != b.recommended) {
            return a.recommended;
        }
        if (a.usable != b.usable) {
            return a.usable;
        }
        if (a.kind != b.kind) {
            return rank(a.kind) < rank(b.kind);
        }
        // localeCompare on dotted addresses: digits and dots compare as the
        // plain strings do.
        return a.address < b.address;
    });
    return addresses;
}

bool isIPv4(std::string_view text) {
    static const boost::regex re(
        "^((25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.){3}(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$");
    return boost::regex_search(text.begin(), text.end(), re);
}

RemoteSettings settingsFromJson(const boost::json::value& value) {
    RemoteSettings settings;
    const boost::json::object* object = value.if_object();
    if (!object) {
        return settings;
    }
    if (const auto* ip = object->if_contains("ip"); ip && ip->is_string()) {
        settings.ip = std::string(ip->get_string());
    }
    if (const auto* port = object->if_contains("port")) {
        if (port->is_int64()) {
            settings.port = static_cast<int>(port->get_int64());
        } else if (port->is_uint64()) {
            settings.port = static_cast<int>(port->get_uint64());
        } else if (port->is_double()) {
            settings.port = static_cast<int>(port->get_double());
        } else if (port->is_string()) {
            // The dialog's number input could save a string.
            try {
                settings.port = std::stoi(std::string(port->get_string()));
            } catch (...) {
            }
        }
    }
    if (const auto* on = object->if_contains("headlessStatus"); on && on->is_bool()) {
        settings.headlessStatus = on->get_bool();
    }
    if (const auto* error = object->if_contains("error"); error && error->is_bool()) {
        settings.error = error->get_bool();
    }
    return settings;
}

boost::json::value settingsToJson(const RemoteSettings& settings) {
    boost::json::object object{
        {"ip", settings.ip},
        {"port", settings.port},
        {"headlessStatus", settings.headlessStatus},
    };
    if (settings.error) {
        object["error"] = true;
    }
    return object;
}

std::string validateSettings(std::string_view ip, int port) {
    if (port < 1025 || port > 65535) {
        return "Invalid Port Number - Must be between 1025 and 65535";
    }
    if (!isIPv4(ip)) {
        return "Invalid IP Address - " + std::string(ip) + " does not look like a valid V4 IP address";
    }
    return {};
}

std::string pendantUrl(std::string_view ip, int port) {
    return "http://" + std::string(ip) + ":" + std::to_string(port) + "/#/remote";
}

}  // namespace gs::remote
