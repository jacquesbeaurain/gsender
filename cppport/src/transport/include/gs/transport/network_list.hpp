#pragma once

// The computer's IPv4 addresses for remote mode's address picker
// (server/lib/network-interfaces.js): what os.networkInterfaces() and the
// default-route probe told upstream. gs::remote ranks them.

#include "gs/remote/network.hpp"

#include <optional>
#include <string>
#include <vector>

namespace gs::transport {

// Every IPv4 address of every interface that is up, loopback included.
// Interface names are the OS's (Windows: the adapter's friendly name, as
// Node reports it - "Wi-Fi", "Ethernet 2").
std::vector<remote::InterfaceAddress> listInterfaceAddresses();

// getDefaultRouteAddress(): the local address the OS would use to reach the
// internet. "Connecting" a UDP socket sends nothing; the kernel only picks
// the route. Empty when there is none (offline).
std::optional<std::string> defaultRouteAddress();

// Both, ranked: the recommended address first.
std::vector<remote::NetworkAddress> listNetworkAddresses();

}  // namespace gs::transport
