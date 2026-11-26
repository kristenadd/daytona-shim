#include "shell_answer.hpp"

#include <utility>

namespace shim {
namespace {

std::uint8_t reverse_bits(std::uint8_t v) {
    std::uint8_t r = 0;
    for (int i = 0; i < 8; ++i)
        if (v & (1u << i)) r |= static_cast<std::uint8_t>(0x80u >> i);
    return r;
}

std::uint32_t pack(const std::array<std::uint8_t, 4>& b) {
    return std::uint32_t(b[0]) | std::uint32_t(b[1]) << 8 | std::uint32_t(b[2]) << 16 | std::uint32_t(b[3]) << 24;
}

std::array<std::uint8_t, 4> unpack(std::uint32_t v) {
    return {static_cast<std::uint8_t>(v), static_cast<std::uint8_t>(v >> 8), static_cast<std::uint8_t>(v >> 16),
            static_cast<std::uint8_t>(v >> 24)};
}

int parity(std::uint32_t v) {
    v ^= v >> 16;
    v ^= v >> 8;
    v ^= v >> 4;
    v ^= v >> 2;
    v ^= v >> 1;
    return static_cast<int>(v & 1u);
}

}  // namespace

std::array<std::uint8_t, 4> shell_check_transform(std::array<std::uint8_t, 4> r) {
    for (std::uint8_t& b : r) b ^= 0xB1;
    // Feedback taps, one bit per byte, the same every round (2019 0x11A6858).
    static constexpr std::uint8_t kTaps[4] = {0x40, 0x08, 0x10, 0x04};
    unsigned m = 13;
    for (int round = 0; round < 12; ++round, --m) {
        unsigned carry = r[0] >> 7;
        for (std::size_t i = 0; i < 4; ++i)
            if (kTaps[i] & r[i]) carry ^= 1;
        std::uint8_t bit = static_cast<std::uint8_t>(1u << (m & 7));
        for (std::size_t k = 4; k-- > 0;) {
            const std::uint8_t b = r[k];
            if (bit == 0) bit = 0x80;
            unsigned out = b >> 7;
            if (bit & 0xA5) out ^= 1;
            r[k] = static_cast<std::uint8_t>(b * 2 + carry);
            bit >>= 1;
            carry = out;
        }
    }
    // The table at 2019 0x11A6750 is the bit reversal; it applies to byte 0 only.
    r[0] = reverse_bits(r[0]);
    for (std::uint8_t& b : r) b ^= 0xC0 ^ 0x77;
    return r;
}

std::array<std::uint8_t, 4> shell_answer(const std::array<std::uint8_t, 4>& challenge) {
    // T(x) = A x + c. Columns of A from the unit vectors, then Gauss-Jordan.
    const std::uint32_t c = pack(shell_check_transform(unpack(0)));
    std::uint32_t rows[32] = {};
    std::uint32_t inv[32] = {};
    std::uint32_t col[32];
    for (int j = 0; j < 32; ++j) col[j] = pack(shell_check_transform(unpack(1u << j))) ^ c;
    for (int i = 0; i < 32; ++i) {
        for (int j = 0; j < 32; ++j)
            if (col[j] & (1u << i)) rows[i] |= 1u << j;
        inv[i] = 1u << i;
    }
    for (int k = 0; k < 32; ++k) {
        int pivot = -1;
        for (int i = k; i < 32; ++i)
            if (rows[i] & (1u << k)) {
                pivot = i;
                break;
            }
        if (pivot < 0) return {0, 0, 0, 0};  // not reached: the transform is invertible (tested)
        std::swap(rows[k], rows[pivot]);
        std::swap(inv[k], inv[pivot]);
        for (int i = 0; i < 32; ++i)
            if (i != k && (rows[i] & (1u << k))) {
                rows[i] ^= rows[k];
                inv[i] ^= inv[k];
            }
    }
    const std::uint32_t y = pack(challenge) ^ c;
    std::uint32_t x = 0;
    for (int k = 0; k < 32; ++k)
        if (parity(inv[k] & y)) x |= 1u << k;
    return unpack(x);
}

}  // namespace shim
