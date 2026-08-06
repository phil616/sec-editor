#pragma once

#include <cstddef>
#include <span>

namespace mempad::encoding {

enum class TextEncoding { ascii, utf8, utf8_bom, utf16_le, utf16_be };
enum class NewlineKind { crlf, lf, cr };

struct NewlineInfo final {
    NewlineKind selected = NewlineKind::crlf;
    std::size_t crlf_count = 0;
    std::size_t lf_count = 0;
    std::size_t cr_count = 0;
    bool mixed = false;
};

struct Analysis final {
    bool ok = false;
    TextEncoding encoding = TextEncoding::utf8;
    NewlineInfo newline{};
    std::size_t utf16_units = 0;
    std::size_t error_offset = 0;
};

Analysis analyze(std::span<const std::byte> input) noexcept;
bool decode(std::span<const std::byte> input, const Analysis& analysis,
            std::span<char16_t> output) noexcept;
bool encoded_size(std::span<const char16_t> input, TextEncoding encoding,
                  NewlineKind newline, std::size_t& size) noexcept;
bool encode(std::span<const char16_t> input, TextEncoding encoding,
            NewlineKind newline, std::span<std::byte> output,
            std::size_t& written) noexcept;
bool contains_non_ascii(std::span<const char16_t> input) noexcept;
const wchar_t* encoding_name(TextEncoding encoding) noexcept;
const wchar_t* newline_name(NewlineKind newline) noexcept;

} // namespace mempad::encoding
