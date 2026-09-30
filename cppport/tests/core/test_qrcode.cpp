// QR codes against react-qr-code / qrcode-generator (tools/gen_qrcode_fixtures.mjs).

#include "gs/util/qrcode.hpp"

#include <gtest/gtest.h>

#include <boost/json.hpp>

#include <fstream>
#include <sstream>
#include <string>

using namespace gs;
namespace json = boost::json;

namespace {

const json::array& cases() {
    static const json::value data = [] {
        std::ifstream in(GS_TEST_DATA_DIR "/qrcode_golden.json", std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        return json::parse(text.str());
    }();
    return data.at("cases").as_array();
}

qr::Level level(std::string_view name) {
    return name == "M" ? qr::Level::M : name == "Q" ? qr::Level::Q : name == "H" ? qr::Level::H : qr::Level::L;
}

std::string hexRow(const qr::Code& code, int row) {
    std::string hex;
    for (int c = 0; c < code.size; c += 4) {
        int v = 0;
        for (int k = 0; k < 4; ++k) {
            v = (v << 1) | (c + k < code.size && code.isDark(row, c + k) ? 1 : 0);
        }
        hex += "0123456789abcdef"[v];
    }
    return hex;
}

}  // namespace

TEST(QrCode, MatchesUpstreamModuleForModule) {
    ASSERT_FALSE(cases().empty());
    for (const json::value& v : cases()) {
        const json::object& c = v.as_object();
        const std::string text(c.at("text").as_string());
        const std::string lvl(c.at("level").as_string());
        SCOPED_TRACE(lvl + " \"" + text + "\"");
        const std::optional<qr::Code> code = qr::encode(text, level(lvl));
        ASSERT_TRUE(code);
        ASSERT_EQ(code->size, c.at("size").as_int64());
        const json::array& rows = c.at("rows").as_array();
        for (int r = 0; r < code->size; ++r) {
            EXPECT_EQ(hexRow(*code, r), std::string(rows[static_cast<std::size_t>(r)].as_string())) << "row " << r;
        }
    }
}

TEST(QrCode, TooLongForVersion40IsEmpty) {
    EXPECT_FALSE(qr::encode(std::string(2954, 'x')));
    EXPECT_TRUE(qr::encode(std::string(2953, 'x')));  // version 40-L's byte capacity
}

TEST(QrCode, SvgDrawsEachDarkModuleInsideTheQuietZone) {
    const qr::Code code = *qr::encode("a");
    const std::string svg = qr::toSvg(code);
    EXPECT_NE(svg.find("viewBox=\"0 0 29 29\""), std::string::npos);  // 21 + 2 * 4
    EXPECT_NE(svg.find("M4 4h1v1h-1z"), std::string::npos);            // the finder's corner
    std::size_t squares = 0;
    for (std::size_t at = svg.find("h1v1"); at != std::string::npos; at = svg.find("h1v1", at + 1)) {
        ++squares;
    }
    std::size_t dark = 0;
    for (bool d : code.dark) {
        dark += d;
    }
    EXPECT_EQ(squares, dark);
}
