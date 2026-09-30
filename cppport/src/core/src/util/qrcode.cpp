#include "gs/util/qrcode.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

// Port of qrcode-generator 2.0.4 (Kazuhiko Arase, MIT), the parts react-qr-code
// uses: byte mode, automatic version, best mask. Kept close to the original
// so the modules match it exactly - including its own "lost point" scoring,
// which differs from the standard's penalty rules and so can pick another
// mask than other encoders would.

namespace gs::qr {
namespace {

// [count, total, data] triples per block group; four rows (L, M, Q, H) per
// version.
struct RsRow {
    std::array<int, 6> v;
};
constexpr RsRow kRsBlocks[] = {
    {{1, 26, 19}}, {{1, 26, 16}}, {{1, 26, 13}}, {{1, 26, 9}},  // 1
    {{1, 44, 34}}, {{1, 44, 28}}, {{1, 44, 22}}, {{1, 44, 16}},  // 2
    {{1, 70, 55}}, {{1, 70, 44}}, {{2, 35, 17}}, {{2, 35, 13}},  // 3
    {{1, 100, 80}}, {{2, 50, 32}}, {{2, 50, 24}}, {{4, 25, 9}},  // 4
    {{1, 134, 108}}, {{2, 67, 43}}, {{2, 33, 15, 2, 34, 16}}, {{2, 33, 11, 2, 34, 12}},  // 5
    {{2, 86, 68}}, {{4, 43, 27}}, {{4, 43, 19}}, {{4, 43, 15}},  // 6
    {{2, 98, 78}}, {{4, 49, 31}}, {{2, 32, 14, 4, 33, 15}}, {{4, 39, 13, 1, 40, 14}},  // 7
    {{2, 121, 97}}, {{2, 60, 38, 2, 61, 39}}, {{4, 40, 18, 2, 41, 19}}, {{4, 40, 14, 2, 41, 15}},  // 8
    {{2, 146, 116}}, {{3, 58, 36, 2, 59, 37}}, {{4, 36, 16, 4, 37, 17}}, {{4, 36, 12, 4, 37, 13}},  // 9
    {{2, 86, 68, 2, 87, 69}}, {{4, 69, 43, 1, 70, 44}}, {{6, 43, 19, 2, 44, 20}}, {{6, 43, 15, 2, 44, 16}},  // 10
    {{4, 101, 81}}, {{1, 80, 50, 4, 81, 51}}, {{4, 50, 22, 4, 51, 23}}, {{3, 36, 12, 8, 37, 13}},  // 11
    {{2, 116, 92, 2, 117, 93}}, {{6, 58, 36, 2, 59, 37}}, {{4, 46, 20, 6, 47, 21}}, {{7, 42, 14, 4, 43, 15}},  // 12
    {{4, 133, 107}}, {{8, 59, 37, 1, 60, 38}}, {{8, 44, 20, 4, 45, 21}}, {{12, 33, 11, 4, 34, 12}},  // 13
    {{3, 145, 115, 1, 146, 116}}, {{4, 64, 40, 5, 65, 41}}, {{11, 36, 16, 5, 37, 17}}, {{11, 36, 12, 5, 37, 13}},  // 14
    {{5, 109, 87, 1, 110, 88}}, {{5, 65, 41, 5, 66, 42}}, {{5, 54, 24, 7, 55, 25}}, {{11, 36, 12, 7, 37, 13}},  // 15
    {{5, 122, 98, 1, 123, 99}}, {{7, 73, 45, 3, 74, 46}}, {{15, 43, 19, 2, 44, 20}}, {{3, 45, 15, 13, 46, 16}},  // 16
    {{1, 135, 107, 5, 136, 108}}, {{10, 74, 46, 1, 75, 47}}, {{1, 50, 22, 15, 51, 23}}, {{2, 42, 14, 17, 43, 15}},  // 17
    {{5, 150, 120, 1, 151, 121}}, {{9, 69, 43, 4, 70, 44}}, {{17, 50, 22, 1, 51, 23}}, {{2, 42, 14, 19, 43, 15}},  // 18
    {{3, 141, 113, 4, 142, 114}}, {{3, 70, 44, 11, 71, 45}}, {{17, 47, 21, 4, 48, 22}}, {{9, 39, 13, 16, 40, 14}},  // 19
    {{3, 135, 107, 5, 136, 108}}, {{3, 67, 41, 13, 68, 42}}, {{15, 54, 24, 5, 55, 25}}, {{15, 43, 15, 10, 44, 16}},  // 20
    {{4, 144, 116, 4, 145, 117}}, {{17, 68, 42}}, {{17, 50, 22, 6, 51, 23}}, {{19, 46, 16, 6, 47, 17}},  // 21
    {{2, 139, 111, 7, 140, 112}}, {{17, 74, 46}}, {{7, 54, 24, 16, 55, 25}}, {{34, 37, 13}},  // 22
    {{4, 151, 121, 5, 152, 122}}, {{4, 75, 47, 14, 76, 48}}, {{11, 54, 24, 14, 55, 25}}, {{16, 45, 15, 14, 46, 16}},  // 23
    {{6, 147, 117, 4, 148, 118}}, {{6, 73, 45, 14, 74, 46}}, {{11, 54, 24, 16, 55, 25}}, {{30, 46, 16, 2, 47, 17}},  // 24
    {{8, 132, 106, 4, 133, 107}}, {{8, 75, 47, 13, 76, 48}}, {{7, 54, 24, 22, 55, 25}}, {{22, 45, 15, 13, 46, 16}},  // 25
    {{10, 142, 114, 2, 143, 115}}, {{19, 74, 46, 4, 75, 47}}, {{28, 50, 22, 6, 51, 23}}, {{33, 46, 16, 4, 47, 17}},  // 26
    {{8, 152, 122, 4, 153, 123}}, {{22, 73, 45, 3, 74, 46}}, {{8, 53, 23, 26, 54, 24}}, {{12, 45, 15, 28, 46, 16}},  // 27
    {{3, 147, 117, 10, 148, 118}}, {{3, 73, 45, 23, 74, 46}}, {{4, 54, 24, 31, 55, 25}}, {{11, 45, 15, 31, 46, 16}},  // 28
    {{7, 146, 116, 7, 147, 117}}, {{21, 73, 45, 7, 74, 46}}, {{1, 53, 23, 37, 54, 24}}, {{19, 45, 15, 26, 46, 16}},  // 29
    {{5, 145, 115, 10, 146, 116}}, {{19, 75, 47, 10, 76, 48}}, {{15, 54, 24, 25, 55, 25}}, {{23, 45, 15, 25, 46, 16}},  // 30
    {{13, 145, 115, 3, 146, 116}}, {{2, 74, 46, 29, 75, 47}}, {{42, 54, 24, 1, 55, 25}}, {{23, 45, 15, 28, 46, 16}},  // 31
    {{17, 145, 115}}, {{10, 74, 46, 23, 75, 47}}, {{10, 54, 24, 35, 55, 25}}, {{19, 45, 15, 35, 46, 16}},  // 32
    {{17, 145, 115, 1, 146, 116}}, {{14, 74, 46, 21, 75, 47}}, {{29, 54, 24, 19, 55, 25}}, {{11, 45, 15, 46, 46, 16}},  // 33
    {{13, 145, 115, 6, 146, 116}}, {{14, 74, 46, 23, 75, 47}}, {{44, 54, 24, 7, 55, 25}}, {{59, 46, 16, 1, 47, 17}},  // 34
    {{12, 151, 121, 7, 152, 122}}, {{12, 75, 47, 26, 76, 48}}, {{39, 54, 24, 14, 55, 25}}, {{22, 45, 15, 41, 46, 16}},  // 35
    {{6, 151, 121, 14, 152, 122}}, {{6, 75, 47, 34, 76, 48}}, {{46, 54, 24, 10, 55, 25}}, {{2, 45, 15, 64, 46, 16}},  // 36
    {{17, 152, 122, 4, 153, 123}}, {{29, 74, 46, 14, 75, 47}}, {{49, 54, 24, 10, 55, 25}}, {{24, 45, 15, 46, 46, 16}},  // 37
    {{4, 152, 122, 18, 153, 123}}, {{13, 74, 46, 32, 75, 47}}, {{48, 54, 24, 14, 55, 25}}, {{42, 45, 15, 32, 46, 16}},  // 38
    {{20, 147, 117, 4, 148, 118}}, {{40, 75, 47, 7, 76, 48}}, {{43, 54, 24, 22, 55, 25}}, {{10, 45, 15, 67, 46, 16}},  // 39
    {{19, 148, 118, 6, 149, 119}}, {{18, 75, 47, 31, 76, 48}}, {{34, 54, 24, 34, 55, 25}}, {{20, 45, 15, 61, 46, 16}},  // 40
};

constexpr int kPatternPositions[40][7] = {
    {},
    {6, 18},
    {6, 22},
    {6, 26},
    {6, 30},
    {6, 34},
    {6, 22, 38},
    {6, 24, 42},
    {6, 26, 46},
    {6, 28, 50},
    {6, 30, 54},
    {6, 32, 58},
    {6, 34, 62},
    {6, 26, 46, 66},
    {6, 26, 48, 70},
    {6, 26, 50, 74},
    {6, 30, 54, 78},
    {6, 30, 56, 82},
    {6, 30, 58, 86},
    {6, 34, 62, 90},
    {6, 28, 50, 72, 94},
    {6, 26, 50, 74, 98},
    {6, 30, 54, 78, 102},
    {6, 28, 54, 80, 106},
    {6, 32, 58, 84, 110},
    {6, 30, 58, 86, 114},
    {6, 34, 62, 90, 118},
    {6, 26, 50, 74, 98, 122},
    {6, 30, 54, 78, 102, 126},
    {6, 26, 52, 78, 104, 130},
    {6, 30, 56, 82, 108, 134},
    {6, 34, 60, 86, 112, 138},
    {6, 30, 58, 86, 114, 142},
    {6, 34, 62, 90, 118, 146},
    {6, 30, 54, 78, 102, 126, 150},
    {6, 24, 50, 76, 102, 128, 154},
    {6, 28, 54, 80, 106, 132, 158},
    {6, 32, 58, 84, 110, 136, 162},
    {6, 26, 54, 82, 110, 138, 166},
    {6, 30, 58, 86, 114, 142, 170},
};

// The level's bits in the format information (QRErrorCorrectionLevel).
int levelBits(Level level) {
    switch (level) {
        case Level::L: return 1;
        case Level::M: return 0;
        case Level::Q: return 3;
        case Level::H: return 2;
    }
    return 1;
}

int levelRow(Level level) {
    return static_cast<int>(level);  // L, M, Q, H: the table's row order
}

struct Block {
    int total = 0;
    int data = 0;
};

std::vector<Block> rsBlocks(int version, Level level) {
    const RsRow& row = kRsBlocks[(version - 1) * 4 + levelRow(level)];
    std::vector<Block> blocks;
    for (int g = 0; g < 2; ++g) {
        for (int i = 0; i < row.v[static_cast<std::size_t>(g * 3)]; ++i) {
            blocks.push_back({row.v[static_cast<std::size_t>(g * 3 + 1)], row.v[static_cast<std::size_t>(g * 3 + 2)]});
        }
    }
    return blocks;
}

int dataCapacityBits(int version, Level level) {
    int total = 0;
    for (const Block& b : rsBlocks(version, level)) {
        total += b.data;
    }
    return total * 8;
}

// Byte mode's character count field.
int lengthBits(int version) {
    return version < 10 ? 8 : 16;
}

// GF(256) with the QR polynomial 0x11D (QRMath).
struct Galois {
    std::array<int, 256> exp{};
    std::array<int, 256> log{};
    Galois() {
        for (int i = 0; i < 8; ++i) {
            exp[static_cast<std::size_t>(i)] = 1 << i;
        }
        for (std::size_t i = 8; i < 256; ++i) {
            exp[i] = exp[i - 4] ^ exp[i - 5] ^ exp[i - 6] ^ exp[i - 8];
        }
        for (std::size_t i = 0; i < 255; ++i) {
            log[static_cast<std::size_t>(exp[i])] = static_cast<int>(i);
        }
    }
    int gexp(int n) const {
        n %= 255;
        if (n < 0) {
            n += 255;
        }
        return exp[static_cast<std::size_t>(n)];
    }
    int mul(int a, int b) const {
        return a == 0 || b == 0 ? 0 : gexp(log[static_cast<std::size_t>(a)] + log[static_cast<std::size_t>(b)]);
    }
};

const Galois& gf() {
    static const Galois g;
    return g;
}

// The error correction bytes: the remainder of data * x^n by the generator
// (x - a^0)...(x - a^(n-1)).
std::vector<int> errorCorrection(const std::vector<int>& data, int count) {
    const Galois& g = gf();
    std::vector<int> generator{1};
    for (int i = 0; i < count; ++i) {
        std::vector<int> next(generator.size() + 1, 0);
        for (std::size_t j = 0; j < generator.size(); ++j) {
            next[j] ^= generator[j];
            next[j + 1] ^= g.mul(generator[j], g.gexp(i));
        }
        generator = std::move(next);
    }
    std::vector<int> rem(static_cast<std::size_t>(count), 0);
    for (int byte : data) {
        const int factor = byte ^ rem[0];
        rem.erase(rem.begin());
        rem.push_back(0);
        for (std::size_t j = 0; j < rem.size(); ++j) {
            rem[j] ^= g.mul(generator[j + 1], factor);
        }
    }
    return rem;
}

class BitBuffer {
public:
    void put(unsigned value, int length) {
        for (int i = 0; i < length; ++i) {
            putBit(((value >> (length - i - 1)) & 1u) == 1u);
        }
    }
    void putBit(bool bit) {
        const std::size_t index = static_cast<std::size_t>(length_ / 8);
        if (bytes_.size() <= index) {
            bytes_.push_back(0);
        }
        if (bit) {
            bytes_[index] |= 0x80 >> (length_ % 8);
        }
        ++length_;
    }
    int length() const noexcept { return length_; }
    const std::vector<int>& bytes() const noexcept { return bytes_; }

private:
    std::vector<int> bytes_;
    int length_ = 0;
};

constexpr unsigned kModeByte = 1u << 2;

void putSegment(BitBuffer& buffer, std::string_view bytes, int version) {
    buffer.put(kModeByte, 4);
    buffer.put(static_cast<unsigned>(bytes.size()), lengthBits(version));
    for (char c : bytes) {
        buffer.put(static_cast<unsigned char>(c), 8);
    }
}

// createData(): the segment, the terminator, padding, then the blocks'
// data and error correction bytes interleaved. Empty on overflow.
std::optional<std::vector<int>> createData(std::string_view bytes, int version, Level level) {
    const std::vector<Block> blocks = rsBlocks(version, level);
    BitBuffer buffer;
    putSegment(buffer, bytes, version);
    const int capacity = dataCapacityBits(version, level);
    if (buffer.length() > capacity) {
        return std::nullopt;
    }
    if (buffer.length() + 4 <= capacity) {
        buffer.put(0, 4);
    }
    while (buffer.length() % 8 != 0) {
        buffer.putBit(false);
    }
    while (buffer.length() < capacity) {
        buffer.put(0xEC, 8);
        if (buffer.length() >= capacity) {
            break;
        }
        buffer.put(0x11, 8);
    }

    std::vector<std::vector<int>> dc;
    std::vector<std::vector<int>> ec;
    std::size_t offset = 0;
    std::size_t maxDc = 0;
    std::size_t maxEc = 0;
    int totalCount = 0;
    for (const Block& b : blocks) {
        std::vector<int> d(buffer.bytes().begin() + static_cast<std::ptrdiff_t>(offset),
                           buffer.bytes().begin() + static_cast<std::ptrdiff_t>(offset + static_cast<std::size_t>(b.data)));
        offset += static_cast<std::size_t>(b.data);
        ec.push_back(errorCorrection(d, b.total - b.data));
        maxDc = std::max(maxDc, d.size());
        maxEc = std::max(maxEc, ec.back().size());
        dc.push_back(std::move(d));
        totalCount += b.total;
    }
    std::vector<int> out;
    out.reserve(static_cast<std::size_t>(totalCount));
    for (std::size_t i = 0; i < maxDc; ++i) {
        for (const auto& d : dc) {
            if (i < d.size()) {
                out.push_back(d[i]);
            }
        }
    }
    for (std::size_t i = 0; i < maxEc; ++i) {
        for (const auto& e : ec) {
            if (i < e.size()) {
                out.push_back(e[i]);
            }
        }
    }
    return out;
}

int bchDigit(unsigned data) {
    int digit = 0;
    while (data != 0) {
        ++digit;
        data >>= 1;
    }
    return digit;
}

constexpr unsigned kG15 = (1u << 10) | (1u << 8) | (1u << 5) | (1u << 4) | (1u << 2) | (1u << 1) | 1u;
constexpr unsigned kG18 = (1u << 12) | (1u << 11) | (1u << 10) | (1u << 9) | (1u << 8) | (1u << 5) | (1u << 2) | 1u;
constexpr unsigned kG15Mask = (1u << 14) | (1u << 12) | (1u << 10) | (1u << 4) | (1u << 1);

unsigned bchTypeInfo(unsigned data) {
    unsigned d = data << 10;
    while (bchDigit(d) - bchDigit(kG15) >= 0) {
        d ^= kG15 << (bchDigit(d) - bchDigit(kG15));
    }
    return ((data << 10) | d) ^ kG15Mask;
}

unsigned bchTypeNumber(unsigned data) {
    unsigned d = data << 12;
    while (bchDigit(d) - bchDigit(kG18) >= 0) {
        d ^= kG18 << (bchDigit(d) - bchDigit(kG18));
    }
    return (data << 12) | d;
}

bool mask(int pattern, int i, int j) {
    switch (pattern) {
        case 0: return (i + j) % 2 == 0;
        case 1: return i % 2 == 0;
        case 2: return j % 3 == 0;
        case 3: return (i + j) % 3 == 0;
        case 4: return (i / 2 + j / 3) % 2 == 0;
        case 5: return (i * j) % 2 + (i * j) % 3 == 0;
        case 6: return ((i * j) % 2 + (i * j) % 3) % 2 == 0;
        default: return ((i * j) % 3 + (i + j) % 2) % 2 == 0;
    }
}

// The symbol under construction: -1 not yet placed, else 0/1.
class Matrix {
public:
    Matrix(int version, Level level, const std::vector<int>& data) : version_(version), level_(level), data_(data) {}

    // makeImpl(): function patterns, format (blank while `test`), then data.
    void make(bool test, int pattern) {
        n_ = version_ * 4 + 17;
        m_.assign(static_cast<std::size_t>(n_ * n_), -1);
        probe(0, 0);
        probe(n_ - 7, 0);
        probe(0, n_ - 7);
        adjust();
        timing();
        typeInfo(test, pattern);
        if (version_ >= 7) {
            typeNumber(test);
        }
        mapData(pattern);
    }

    int size() const noexcept { return n_; }
    bool dark(int r, int c) const { return m_[static_cast<std::size_t>(r * n_ + c)] == 1; }

    // QRUtil.getLostPoint().
    double lostPoint() const {
        const int n = n_;
        double lost = 0;
        for (int row = 0; row < n; ++row) {
            for (int col = 0; col < n; ++col) {
                int same = 0;
                const bool d = dark(row, col);
                for (int r = -1; r <= 1; ++r) {
                    if (row + r < 0 || n <= row + r) {
                        continue;
                    }
                    for (int c = -1; c <= 1; ++c) {
                        if (col + c < 0 || n <= col + c || (r == 0 && c == 0)) {
                            continue;
                        }
                        if (d == dark(row + r, col + c)) {
                            ++same;
                        }
                    }
                }
                if (same > 5) {
                    lost += 3 + same - 5;
                }
            }
        }
        for (int row = 0; row < n - 1; ++row) {
            for (int col = 0; col < n - 1; ++col) {
                const int count = dark(row, col) + dark(row + 1, col) + dark(row, col + 1) + dark(row + 1, col + 1);
                if (count == 0 || count == 4) {
                    lost += 3;
                }
            }
        }
        const auto finder = [&](auto at) {
            return at(0) && !at(1) && at(2) && at(3) && at(4) && !at(5) && at(6);
        };
        for (int row = 0; row < n; ++row) {
            for (int col = 0; col < n - 6; ++col) {
                if (finder([&](int k) { return dark(row, col + k); })) {
                    lost += 40;
                }
            }
        }
        for (int col = 0; col < n; ++col) {
            for (int row = 0; row < n - 6; ++row) {
                if (finder([&](int k) { return dark(row + k, col); })) {
                    lost += 40;
                }
            }
        }
        int darkCount = 0;
        for (int i = 0; i < n * n; ++i) {
            darkCount += m_[static_cast<std::size_t>(i)] == 1;
        }
        const double ratio = std::abs(100.0 * darkCount / n / n - 50) / 5;
        return lost + ratio * 10;
    }

private:
    int& at(int r, int c) { return m_[static_cast<std::size_t>(r * n_ + c)]; }

    void probe(int row, int col) {
        for (int r = -1; r <= 7; ++r) {
            if (row + r <= -1 || n_ <= row + r) {
                continue;
            }
            for (int c = -1; c <= 7; ++c) {
                if (col + c <= -1 || n_ <= col + c) {
                    continue;
                }
                const bool on = (0 <= r && r <= 6 && (c == 0 || c == 6)) || (0 <= c && c <= 6 && (r == 0 || r == 6)) ||
                                (2 <= r && r <= 4 && 2 <= c && c <= 4);
                at(row + r, col + c) = on;
            }
        }
    }

    void adjust() {
        const int* pos = kPatternPositions[version_ - 1];
        int count = 0;
        while (count < 7 && pos[count] != 0) {
            ++count;
        }
        for (int i = 0; i < count; ++i) {
            for (int j = 0; j < count; ++j) {
                const int row = pos[i];
                const int col = pos[j];
                if (at(row, col) != -1) {
                    continue;
                }
                for (int r = -2; r <= 2; ++r) {
                    for (int c = -2; c <= 2; ++c) {
                        at(row + r, col + c) = r == -2 || r == 2 || c == -2 || c == 2 || (r == 0 && c == 0);
                    }
                }
            }
        }
    }

    void timing() {
        for (int r = 8; r < n_ - 8; ++r) {
            if (at(r, 6) == -1) {
                at(r, 6) = r % 2 == 0;
            }
        }
        for (int c = 8; c < n_ - 8; ++c) {
            if (at(6, c) == -1) {
                at(6, c) = c % 2 == 0;
            }
        }
    }

    void typeNumber(bool test) {
        const unsigned bits = bchTypeNumber(static_cast<unsigned>(version_));
        for (int i = 0; i < 18; ++i) {
            const bool on = !test && ((bits >> i) & 1u) == 1u;
            at(i / 3, i % 3 + n_ - 8 - 3) = on;
            at(i % 3 + n_ - 8 - 3, i / 3) = on;
        }
    }

    void typeInfo(bool test, int pattern) {
        const unsigned bits = bchTypeInfo(static_cast<unsigned>((levelBits(level_) << 3) | pattern));
        for (int i = 0; i < 15; ++i) {
            const bool on = !test && ((bits >> i) & 1u) == 1u;
            if (i < 6) {
                at(i, 8) = on;
            } else if (i < 8) {
                at(i + 1, 8) = on;
            } else {
                at(n_ - 15 + i, 8) = on;
            }
        }
        for (int i = 0; i < 15; ++i) {
            const bool on = !test && ((bits >> i) & 1u) == 1u;
            if (i < 8) {
                at(8, n_ - i - 1) = on;
            } else if (i < 9) {
                at(8, 15 - i - 1 + 1) = on;
            } else {
                at(8, 15 - i - 1) = on;
            }
        }
        at(n_ - 8, 8) = !test;
    }

    void mapData(int pattern) {
        int inc = -1;
        int row = n_ - 1;
        int bitIndex = 7;
        std::size_t byteIndex = 0;
        for (int col = n_ - 1; col > 0; col -= 2) {
            if (col == 6) {
                col -= 1;
            }
            while (true) {
                for (int c = 0; c < 2; ++c) {
                    if (at(row, col - c) == -1) {
                        bool on = false;
                        if (byteIndex < data_.size()) {
                            on = ((data_[byteIndex] >> bitIndex) & 1) == 1;
                        }
                        if (mask(pattern, row, col - c)) {
                            on = !on;
                        }
                        at(row, col - c) = on;
                        if (--bitIndex == -1) {
                            ++byteIndex;
                            bitIndex = 7;
                        }
                    }
                }
                row += inc;
                if (row < 0 || n_ <= row) {
                    row -= inc;
                    inc = -inc;
                    break;
                }
            }
        }
    }

    int version_;
    Level level_;
    const std::vector<int>& data_;
    int n_ = 0;
    std::vector<int> m_;
};

}  // namespace

std::optional<Code> encode(std::string_view utf8, Level level) {
    // make(): the first version whose capacity holds the segment (40 when
    // none does - which then overflows).
    int version = 1;
    for (; version < 40; ++version) {
        BitBuffer buffer;
        putSegment(buffer, utf8, version);
        if (buffer.length() <= dataCapacityBits(version, level)) {
            break;
        }
    }
    const std::optional<std::vector<int>> data = createData(utf8, version, level);
    if (!data) {
        return std::nullopt;
    }
    Matrix matrix(version, level, *data);
    double minLost = 0;
    int best = 0;
    for (int i = 0; i < 8; ++i) {
        matrix.make(true, i);
        const double lost = matrix.lostPoint();
        if (i == 0 || minLost > lost) {
            minLost = lost;
            best = i;
        }
    }
    matrix.make(false, best);

    Code code;
    code.size = matrix.size();
    code.dark.resize(static_cast<std::size_t>(code.size * code.size));
    for (int r = 0; r < code.size; ++r) {
        for (int c = 0; c < code.size; ++c) {
            code.dark[static_cast<std::size_t>(r * code.size + c)] = matrix.dark(r, c);
        }
    }
    return code;
}

std::string toSvg(const Code& code, int quietZone) {
    const int total = code.size + 2 * quietZone;
    std::string svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 " + std::to_string(total) + " " +
                      std::to_string(total) + "\" shape-rendering=\"crispEdges\">";
    svg += "<rect width=\"100%\" height=\"100%\" fill=\"#fff\"/><path fill=\"#000\" d=\"";
    for (int r = 0; r < code.size; ++r) {
        for (int c = 0; c < code.size; ++c) {
            if (code.isDark(r, c)) {
                svg += "M" + std::to_string(c + quietZone) + " " + std::to_string(r + quietZone) + "h1v1h-1z";
            }
        }
    }
    svg += "\"/></svg>";
    return svg;
}

}  // namespace gs::qr
