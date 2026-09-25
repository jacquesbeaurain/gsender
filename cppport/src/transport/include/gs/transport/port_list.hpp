#pragma once

// Serial port enumeration (node-serialport's SerialPort.list()) and gSender's
// split into recognized and other ports (the "list" handler in CNCEngine.js).

#include <string>
#include <string_view>
#include <vector>

namespace gs::transport {

struct SerialPortInfo {
    std::string path;          // "COM3"; Linux: "/dev/ttyACM0"
    std::string manufacturer;  // "Arduino LLC (www.arduino.cc)"
    std::string friendlyName;  // "Arduino Uno (COM3)"
    std::string pnpId;         // "USB\VID_2341&PID_0043\7543..."; Linux: the /dev/serial/by-id name
    std::string vendorId;      // "2341" (upper-case hex; empty when unknown)
    std::string productId;     // "0043"
};

// The serial ports present now: SetupAPI on Windows, sysfs on Linux (empty
// elsewhere).
std::vector<SerialPortInfo> listSerialPorts();

// The Linux listing from the given roots (normally /sys/class/tty,
// /dev/serial/by-id and /dev), so it can be tested against a fake tree.
std::vector<SerialPortInfo> listSerialPortsFromSysfs(const std::string& ttyClassDir, const std::string& byIdDir,
                                                     const std::string& devDir);

// USB vendor/product id pairs of the boards gSender lists first
// (case-insensitive; a port without ids is never recognized).
bool isRecognizedPort(std::string_view vendorId, std::string_view productId);

// "VID_2341" / "PID_0043" in a PnP or hardware id; empty when absent.
std::string usbId(std::string_view pnpId, std::string_view key);

}  // namespace gs::transport
