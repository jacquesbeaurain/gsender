#pragma once

// The Windows port and network listings' wide-string conversion.

#ifdef _WIN32
#include <windows.h>

#include <string>

namespace gs::transport {

// UTF-16 from a Windows API to UTF-8; empty for null or empty text.
inline std::string narrow(const wchar_t* text) {
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

}  // namespace gs::transport
#endif
