#include "gs/transport/port_list.hpp"

#include "gs/util/strings.hpp"

#include <algorithm>
#include <array>

#ifdef _WIN32
#include <windows.h>
#include <setupapi.h>
#endif

namespace gs::transport {
namespace {

// CNCEngine.js validProductIDs / validVendorIDs.
constexpr std::array<std::string_view, 17> kProductIds{"000A", "0483", "6015", "6001", "606D", "003D",
                                                       "0042", "0043", "2341", "7523", "EA60", "2303",
                                                       "2145", "0AD8", "08D8", "5740", "0FA7"};
constexpr std::array<std::string_view, 12> kVendorIds{"2E8A", "16C0", "1D50", "0403", "2341", "0042",
                                                      "1A86", "10C4", "067B", "03EB", "16D0", "0483"};

template <std::size_t N>
bool containsIgnoringCase(const std::array<std::string_view, N>& list, std::string_view value) {
    return std::any_of(list.begin(), list.end(), [value](std::string_view item) { return str::iequals(item, value); });
}

#ifdef _WIN32
// GUID_DEVCLASS_PORTS (devguid.h): the "Ports (COM & LPT)" device class.
constexpr GUID kPortsClass = {0x4D36E978, 0xE325, 0x11CE, {0xBF, 0xC1, 0x08, 0x00, 0x2B, 0xE1, 0x03, 0x18}};

std::string narrow(const wchar_t* text) {
    if (!text || !*text) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) {
        return {};
    }
    std::string out(static_cast<std::size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), size, nullptr, nullptr);
    return out;
}

// A string registry property of a device (the first entry of a multi-string).
std::string deviceProperty(HDEVINFO devices, SP_DEVINFO_DATA& device, DWORD property) {
    DWORD size = 0;
    SetupDiGetDeviceRegistryPropertyW(devices, &device, property, nullptr, nullptr, 0, &size);
    if (size == 0) {
        return {};
    }
    std::vector<wchar_t> buffer(size / sizeof(wchar_t) + 2, L'\0');
    if (!SetupDiGetDeviceRegistryPropertyW(devices, &device, property, nullptr,
                                           reinterpret_cast<PBYTE>(buffer.data()),
                                           static_cast<DWORD>(buffer.size() * sizeof(wchar_t)), nullptr)) {
        return {};
    }
    return narrow(buffer.data());
}

std::string portName(HDEVINFO devices, SP_DEVINFO_DATA& device) {
    HKEY key = SetupDiOpenDevRegKey(devices, &device, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
    if (key == INVALID_HANDLE_VALUE) {
        return {};
    }
    std::array<wchar_t, 256> name{};
    DWORD size = static_cast<DWORD>((name.size() - 1) * sizeof(wchar_t));
    DWORD type = 0;
    const LONG result =
        RegQueryValueExW(key, L"PortName", nullptr, &type, reinterpret_cast<LPBYTE>(name.data()), &size);
    RegCloseKey(key);
    return result == ERROR_SUCCESS && type == REG_SZ ? narrow(name.data()) : std::string();
}
#endif

}  // namespace

std::string usbId(std::string_view pnpId, std::string_view key) {
    const std::string upper = str::toUpper(pnpId);
    const std::string needle = str::toUpper(key) + "_";
    const std::size_t at = upper.find(needle);
    if (at == std::string::npos || at + needle.size() + 4 > upper.size()) {
        return {};
    }
    const std::string id = upper.substr(at + needle.size(), 4);
    const bool hex = std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F');
    });
    return hex ? id : std::string();
}

bool isRecognizedPort(std::string_view vendorId, std::string_view productId) {
    if (vendorId.empty() || productId.empty()) {
        return false;
    }
    return containsIgnoringCase(kProductIds, productId) && containsIgnoringCase(kVendorIds, vendorId);
}

std::vector<SerialPortInfo> listSerialPorts() {
    std::vector<SerialPortInfo> ports;
#ifdef _WIN32
    HDEVINFO devices = SetupDiGetClassDevsW(&kPortsClass, nullptr, nullptr, DIGCF_PRESENT);
    if (devices == INVALID_HANDLE_VALUE) {
        return ports;
    }
    SP_DEVINFO_DATA device{};
    device.cbSize = sizeof(device);
    for (DWORD index = 0; SetupDiEnumDeviceInfo(devices, index, &device); ++index) {
        SerialPortInfo info;
        info.path = portName(devices, device);
        if (!info.path.starts_with("COM")) {
            continue;  // the Ports class holds printer (LPT) ports too
        }
        info.manufacturer = deviceProperty(devices, device, SPDRP_MFG);
        info.friendlyName = deviceProperty(devices, device, SPDRP_FRIENDLYNAME);
        std::array<wchar_t, 512> instance{};
        if (SetupDiGetDeviceInstanceIdW(devices, &device, instance.data(), static_cast<DWORD>(instance.size()),
                                        nullptr)) {
            info.pnpId = narrow(instance.data());
        }
        info.vendorId = usbId(info.pnpId, "VID");
        info.productId = usbId(info.pnpId, "PID");
        ports.push_back(std::move(info));
    }
    SetupDiDestroyDeviceInfoList(devices);
#endif
    return ports;
}

}  // namespace gs::transport
