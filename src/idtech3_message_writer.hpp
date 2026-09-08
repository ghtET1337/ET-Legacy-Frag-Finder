// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "idtech3_huffman.hpp"
#include <array>
#include <cstdlib>
#include <stdexcept>
#include <string_view>
#include <ostream>

namespace etlfrag::detail {
class MessageWriter {
public:
    explicit MessageWriter(const HuffmanDecoder& codec) : codec_(codec) {}
    void bits(std::int32_t value, int width) {
        width = std::abs(width);
        if (width < 1 || width > 32) throw std::runtime_error("Invalid field width");
        const auto raw = static_cast<std::uint32_t>(value);
        const int remainder = width & 7;
        for (int i = 0; i < remainder; ++i) {
            if (offset_ >= maximumBits()) throw std::runtime_error("Cut demo message is too large");
            if ((raw >> i) & 1u) data_[offset_ >> 3] |= std::uint8_t(1u << (offset_ & 7));
            ++offset_;
        }
        for (int i = remainder; i < width; i += 8)
            codec_.transmit(static_cast<std::uint8_t>(raw >> i), data_.data(), offset_, maximumBits());
    }
    void byte(int value) { bits(value, 8); }
    void shortValue(int value) { bits(value, 16); }
    void longValue(std::int32_t value) { bits(value, 32); }
    void string(std::string_view value) {
        for (unsigned char c : value) byte(c);
        byte(0);
    }
    static void littleLong(std::ostream& out, std::int32_t value) {
        const auto raw = static_cast<std::uint32_t>(value);
        for (int i = 0; i < 4; ++i) out.put(static_cast<char>((raw >> (8 * i)) & 255));
    }
    void packet(std::ostream& out, int sequence) const {
        // MSG_WriteBits uses (bit >> 3) + 1, including the trailing pad byte.
        const int size = (offset_ >> 3) + 1;
        littleLong(out, sequence);
        littleLong(out, size);
        out.write(reinterpret_cast<const char*>(data_.data()), size);
        if (!out) throw std::runtime_error("Could not write cut demo (check free disk space)");
    }
private:
    static constexpr int maximumBits() { return 32768 * 8 - 1; }
    const HuffmanDecoder& codec_;
    std::array<std::uint8_t, 32768> data_{};
    int offset_ = 0;
};
} // namespace etlfrag::detail
