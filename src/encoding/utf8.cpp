#include "encoding/utf8.h"

#include <cstdint>

namespace mempad::encoding {
namespace {
std::uint8_t byte_at(const std::span<const std::byte> bytes,
                     const std::size_t index) noexcept {
    return std::to_integer<std::uint8_t>(bytes[index]);
}

bool continuation(const std::uint8_t value) noexcept {
    return (value & 0xC0U) == 0x80U;
}
} // namespace

bool next_utf8(const std::span<const std::byte> bytes, const std::size_t offset,
               Utf8Scalar& scalar) noexcept {
    if (offset >= bytes.size()) {
        return false;
    }
    const std::uint8_t first = byte_at(bytes, offset);
    if (first < 0x80U) {
        scalar = {static_cast<char32_t>(first), 1};
        return true;
    }
    if (first >= 0xC2U && first <= 0xDFU) {
        if (offset + 1U >= bytes.size()) {
            return false;
        }
        const std::uint8_t second = byte_at(bytes, offset + 1U);
        if (!continuation(second)) {
            return false;
        }
        scalar = {static_cast<char32_t>(((first & 0x1FU) << 6U) |
                                        (second & 0x3FU)), 2};
        return true;
    }
    if (first >= 0xE0U && first <= 0xEFU) {
        if (offset + 2U >= bytes.size()) {
            return false;
        }
        const std::uint8_t second = byte_at(bytes, offset + 1U);
        const std::uint8_t third = byte_at(bytes, offset + 2U);
        if (!continuation(second) || !continuation(third) ||
            (first == 0xE0U && second < 0xA0U) ||
            (first == 0xEDU && second >= 0xA0U)) {
            return false;
        }
        scalar = {static_cast<char32_t>(((first & 0x0FU) << 12U) |
                                        ((second & 0x3FU) << 6U) |
                                        (third & 0x3FU)), 3};
        return true;
    }
    if (first >= 0xF0U && first <= 0xF4U) {
        if (offset + 3U >= bytes.size()) {
            return false;
        }
        const std::uint8_t second = byte_at(bytes, offset + 1U);
        const std::uint8_t third = byte_at(bytes, offset + 2U);
        const std::uint8_t fourth = byte_at(bytes, offset + 3U);
        if (!continuation(second) || !continuation(third) ||
            !continuation(fourth) ||
            (first == 0xF0U && second < 0x90U) ||
            (first == 0xF4U && second >= 0x90U)) {
            return false;
        }
        scalar = {static_cast<char32_t>(((first & 0x07U) << 18U) |
                                        ((second & 0x3FU) << 12U) |
                                        ((third & 0x3FU) << 6U) |
                                        (fourth & 0x3FU)), 4};
        return true;
    }
    return false;
}

std::size_t utf8_width(const char32_t scalar) noexcept {
    if (scalar <= 0x7FU) return 1;
    if (scalar <= 0x7FFU) return 2;
    if (scalar >= 0xD800U && scalar <= 0xDFFFU) return 0;
    if (scalar <= 0xFFFFU) return 3;
    if (scalar <= 0x10FFFFU) return 4;
    return 0;
}

bool write_utf8(const char32_t scalar, const std::span<std::byte> output,
                std::size_t& offset) noexcept {
    const std::size_t width = utf8_width(scalar);
    if (width == 0 || offset > output.size() || width > output.size() - offset) {
        return false;
    }
    if (width == 1) {
        output[offset++] = static_cast<std::byte>(scalar);
    } else if (width == 2) {
        output[offset++] = static_cast<std::byte>(0xC0U | (scalar >> 6U));
        output[offset++] = static_cast<std::byte>(0x80U | (scalar & 0x3FU));
    } else if (width == 3) {
        output[offset++] = static_cast<std::byte>(0xE0U | (scalar >> 12U));
        output[offset++] = static_cast<std::byte>(0x80U | ((scalar >> 6U) & 0x3FU));
        output[offset++] = static_cast<std::byte>(0x80U | (scalar & 0x3FU));
    } else {
        output[offset++] = static_cast<std::byte>(0xF0U | (scalar >> 18U));
        output[offset++] = static_cast<std::byte>(0x80U | ((scalar >> 12U) & 0x3FU));
        output[offset++] = static_cast<std::byte>(0x80U | ((scalar >> 6U) & 0x3FU));
        output[offset++] = static_cast<std::byte>(0x80U | (scalar & 0x3FU));
    }
    return true;
}

} // namespace mempad::encoding
