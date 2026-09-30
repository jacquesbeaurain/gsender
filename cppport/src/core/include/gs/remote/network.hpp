#pragma once

// Remote mode's addresses and settings (features/RemoteMode,
// server/lib/network-interfaces.js, server/api/api.remote.js): which of the
// computer's IPv4 addresses a phone on the shop network can reach, the one
// to recommend, and the saved "remoteSettings". Enumerating the interfaces
// and probing the default route is the transport's job; the rules are here.

#include <boost/json/value.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gs::remote {

// Sort rank within the address list: lower comes first.
enum class AddressKind { Wifi = 0, Ethernet = 1, Unknown = 2, Virtual = 3, Loopback = 4 };

// One IPv4 address of one interface, as the OS reports it.
struct InterfaceAddress {
    std::string iface;
    std::string address;
    bool internal = false;  // loopback
};

struct NetworkAddress {
    std::string address;
    std::string iface;
    AddressKind kind = AddressKind::Unknown;
    std::string label;          // "Wi-Fi", "Ethernet", "This computer only"...
    bool usable = false;        // another device can reach it
    bool recommended = false;
};

// classify(): loopback, then virtual (hypervisors, containers, VPNs), Wi-Fi
// and Ethernet by the interface's name.
AddressKind classify(std::string_view iface, bool internal);
std::string kindLabel(AddressKind kind);

// listNetworkAddresses(): one entry per address (the best-classified
// interface wins), the recommended one first - the default route's address
// when it is usable, else the best private address - then usable ones, by
// kind, then by address.
std::vector<NetworkAddress> rankAddresses(const std::vector<InterfaceAddress>& interfaces,
                                          const std::optional<std::string>& defaultRouteAddress);

// app/lib/utils isIPv4(): four dot-separated decimal numbers 0-255.
bool isIPv4(std::string_view text);

// The saved settings (config key "remoteSettings"): the address and port the
// pendant is served on and whether remote mode is on. `error` is set when
// the address could not be bound (remote mode is then switched off).
struct RemoteSettings {
    std::string ip;
    int port = 8000;
    bool headlessStatus = false;
    bool error = false;
};

inline constexpr std::string_view kSettingsKey = "remoteSettings";

RemoteSettings settingsFromJson(const boost::json::value& value);
boost::json::value settingsToJson(const RemoteSettings& settings);

// onConfirmUpdate()'s checks: empty when the settings may be saved, else the
// message upstream toasts.
std::string validateSettings(std::string_view ip, int port);

// What the QR code holds (QRCodeDisplay): the pendant's address on a phone.
std::string pendantUrl(std::string_view ip, int port);

}  // namespace gs::remote
