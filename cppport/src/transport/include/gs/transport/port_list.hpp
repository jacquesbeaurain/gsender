#pragma once

// Serial port enumeration (node-serialport's SerialPort.list()) and gSender's
// split into recognized and other ports (the "list" handler in CNCEngine.js).

#include <string>
#include <string_view>
#include <vector>

namespace gs::transport {

struct SerialPortInfo {
    std::string path;          // "COM3"
    std::string manufacturer;  // "Arduino LLC (www.arduino.cc)"
    std::string friendlyName;  // "Arduino Uno (COM3)"
    std::string pnpId;         // "USB\VID_2341&PID_0043\7543..."
    std::string vendorId;      // "2341" (upper-case hex; empty when unknown)
    std::string productId;     // "0043"
};

// The serial ports present now. Windows only so far (SetupAPI); elsewhere empty.
std::vector<SerialPortInfo> listSerialPorts();

// USB vendor/product id pairs of the boards gSender lists first
// (case-insensitive; a port without ids is never recognized).
bool isRecognizedPort(std::string_view vendorId, std::string_view productId);

// "VID_2341" / "PID_0043" in a PnP or hardware id; empty when absent.
std::string usbId(std::string_view pnpId, std::string_view key);

}  // namespace gs::transport
