#pragma once

// QR codes, as gSender draws them: react-qr-code runs Kazuhiko Arase's
// qrcode-generator (MIT) on the text's UTF-8 bytes in byte mode, at the
// smallest version that holds them, with the mask whose "lost points" are
// lowest. Ported module for module (tests/data/qrcode_golden.json), so the
// port's codes are the ones upstream shows for the same text.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gs::qr {

enum class Level { L, M, Q, H };  // react-qr-code's default is L

struct Code {
    int size = 0;             // modules per side (version * 4 + 17)
    std::vector<bool> dark;   // row-major, size * size

    bool isDark(int row, int col) const { return dark[static_cast<std::size_t>(row * size + col)]; }
};

// Empty when the text does not fit version 40.
std::optional<Code> encode(std::string_view utf8, Level level = Level::L);

// The code as an SVG image: one path of unit squares on a white background,
// `quietZone` modules of margin (react-qr-code draws none; readers want 4).
std::string toSvg(const Code& code, int quietZone = 4);

}  // namespace gs::qr
