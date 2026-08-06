#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace mempad::encoding {

struct Utf8Scalar final {
    char32_t value = 0;
    std::size_t width = 0;
};

bool next_utf8(std::span<const std::byte> bytes, std::size_t offset,
               Utf8Scalar& scalar) noexcept;
std::size_t utf8_width(char32_t scalar) noexcept;
bool write_utf8(char32_t scalar, std::span<std::byte> output,
                std::size_t& offset) noexcept;

} // namespace mempad::encoding
