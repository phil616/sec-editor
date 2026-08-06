#include "encoding/encoding.h"

#include "encoding/utf8.h"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace mempad::encoding {
namespace {
bool high_surrogate(const char16_t value) noexcept {
    return value >= 0xD800U && value <= 0xDBFFU;
}
bool low_surrogate(const char16_t value) noexcept {
    return value >= 0xDC00U && value <= 0xDFFFU;
}

struct ScalarReader final {
    std::span<const std::byte> input;
    TextEncoding encoding;
    std::size_t offset;

    bool next(char32_t& value, std::size_t& error) noexcept {
        if (offset >= input.size()) return false;
        if (encoding == TextEncoding::ascii || encoding == TextEncoding::utf8 ||
            encoding == TextEncoding::utf8_bom) {
            Utf8Scalar scalar{};
            if (!next_utf8(input, offset, scalar)) {
                error = offset;
                offset = input.size() + 1U;
                return false;
            }
            value = scalar.value;
            offset += scalar.width;
            return true;
        }
        if (offset + 1U >= input.size()) {
            error = offset;
            offset = input.size() + 1U;
            return false;
        }
        const auto first_byte = std::to_integer<std::uint8_t>(input[offset]);
        const auto second_byte = std::to_integer<std::uint8_t>(input[offset + 1U]);
        const char16_t first = encoding == TextEncoding::utf16_le
            ? static_cast<char16_t>(first_byte | (static_cast<unsigned>(second_byte) << 8U))
            : static_cast<char16_t>((static_cast<unsigned>(first_byte) << 8U) | second_byte);
        offset += 2U;
        if (high_surrogate(first)) {
            if (offset + 1U >= input.size()) {
                error = offset - 2U;
                offset = input.size() + 1U;
                return false;
            }
            const auto b0 = std::to_integer<std::uint8_t>(input[offset]);
            const auto b1 = std::to_integer<std::uint8_t>(input[offset + 1U]);
            const char16_t second = encoding == TextEncoding::utf16_le
                ? static_cast<char16_t>(b0 | (static_cast<unsigned>(b1) << 8U))
                : static_cast<char16_t>((static_cast<unsigned>(b0) << 8U) | b1);
            if (!low_surrogate(second)) {
                error = offset;
                offset = input.size() + 1U;
                return false;
            }
            offset += 2U;
            value = 0x10000U + ((static_cast<char32_t>(first) - 0xD800U) << 10U) +
                    (static_cast<char32_t>(second) - 0xDC00U);
            return true;
        }
        if (low_surrogate(first)) {
            error = offset - 2U;
            offset = input.size() + 1U;
            return false;
        }
        value = first;
        return true;
    }
};

std::size_t bom_size(const TextEncoding encoding) noexcept {
    if (encoding == TextEncoding::utf8_bom) return 3;
    if (encoding == TextEncoding::utf16_le || encoding == TextEncoding::utf16_be) return 2;
    return 0;
}

bool add_checked(std::size_t& value, const std::size_t amount) noexcept {
    if (amount > std::numeric_limits<std::size_t>::max() - value) return false;
    value += amount;
    return true;
}

void finish_newlines(NewlineInfo& info) noexcept {
    const std::size_t kinds = (info.crlf_count != 0 ? 1U : 0U) +
                              (info.lf_count != 0 ? 1U : 0U) +
                              (info.cr_count != 0 ? 1U : 0U);
    info.mixed = kinds > 1U;
    if (info.crlf_count == 0 && info.lf_count == 0 && info.cr_count == 0) {
        info.selected = NewlineKind::crlf;
    } else if (info.crlf_count >= info.lf_count && info.crlf_count >= info.cr_count) {
        info.selected = NewlineKind::crlf;
    } else if (info.lf_count >= info.cr_count) {
        info.selected = NewlineKind::lf;
    } else {
        info.selected = NewlineKind::cr;
    }
}

bool next_utf16(const std::span<const char16_t> input, std::size_t& offset,
                char32_t& scalar) noexcept {
    if (offset >= input.size()) return false;
    const char16_t first = input[offset++];
    if (high_surrogate(first)) {
        if (offset >= input.size() || !low_surrogate(input[offset])) return false;
        const char16_t second = input[offset++];
        scalar = 0x10000U + ((static_cast<char32_t>(first) - 0xD800U) << 10U) +
                 (static_cast<char32_t>(second) - 0xDC00U);
        return true;
    }
    if (low_surrogate(first)) return false;
    scalar = first;
    return true;
}

std::size_t scalar_units(const char32_t scalar) noexcept {
    return scalar > 0xFFFFU ? 2U : 1U;
}

bool put_utf16(const char32_t scalar, const std::span<char16_t> output,
               std::size_t& offset) noexcept {
    const std::size_t needed = scalar_units(scalar);
    if (offset > output.size() || needed > output.size() - offset) return false;
    if (needed == 1U) {
        output[offset++] = static_cast<char16_t>(scalar);
    } else {
        const char32_t adjusted = scalar - 0x10000U;
        output[offset++] = static_cast<char16_t>(0xD800U + (adjusted >> 10U));
        output[offset++] = static_cast<char16_t>(0xDC00U + (adjusted & 0x3FFU));
    }
    return true;
}

bool write_u16(const char32_t scalar, const bool little,
               const std::span<std::byte> output, std::size_t& offset) noexcept {
    const std::size_t count = scalar_units(scalar);
    if (scalar > 0x10FFFFU || (scalar >= 0xD800U && scalar <= 0xDFFFU) ||
        offset > output.size() || count * 2U > output.size() - offset) return false;
    for (std::size_t i = 0; i < count; ++i) {
        const char32_t adjusted = scalar > 0xFFFFU ? scalar - 0x10000U : scalar;
        const auto value = static_cast<std::uint16_t>(count == 1U ? adjusted :
            (i == 0 ? 0xD800U + (adjusted >> 10U)
                    : 0xDC00U + (adjusted & 0x3FFU)));
        output[offset++] = static_cast<std::byte>(little ? value & 0xFFU : value >> 8U);
        output[offset++] = static_cast<std::byte>(little ? value >> 8U : value & 0xFFU);
    }
    return true;
}

bool write_encoded_scalar(const char32_t scalar, const TextEncoding encoding,
                          const std::span<std::byte> output,
                          std::size_t& offset) noexcept {
    if (encoding == TextEncoding::utf16_le || encoding == TextEncoding::utf16_be) {
        return write_u16(scalar, encoding == TextEncoding::utf16_le, output, offset);
    }
    return write_utf8(scalar, output, offset);
}
} // namespace

Analysis analyze(const std::span<const std::byte> input) noexcept {
    Analysis result{};
    std::size_t start = 0;
    if (input.size() >= 3U && input[0] == std::byte{0xEF} &&
        input[1] == std::byte{0xBB} && input[2] == std::byte{0xBF}) {
        result.encoding = TextEncoding::utf8_bom;
        start = 3;
    } else if (input.size() >= 2U && input[0] == std::byte{0xFF} &&
               input[1] == std::byte{0xFE}) {
        result.encoding = TextEncoding::utf16_le;
        start = 2;
    } else if (input.size() >= 2U && input[0] == std::byte{0xFE} &&
               input[1] == std::byte{0xFF}) {
        result.encoding = TextEncoding::utf16_be;
        start = 2;
    } else {
        bool ascii = true;
        for (const std::byte value : input) {
            if (std::to_integer<unsigned>(value) >= 0x80U) {
                ascii = false;
                break;
            }
        }
        result.encoding = ascii ? TextEncoding::ascii : TextEncoding::utf8;
    }
    if ((result.encoding == TextEncoding::utf16_le ||
         result.encoding == TextEncoding::utf16_be) && ((input.size() - start) % 2U != 0)) {
        result.error_offset = input.size() - 1U;
        return result;
    }
    ScalarReader reader{input, result.encoding, start};
    bool pending_cr = false;
    while (reader.offset < input.size()) {
        char32_t scalar = 0;
        std::size_t error = reader.offset;
        if (!reader.next(scalar, error)) {
            result.error_offset = error;
            return result;
        }
        if (scalar == U'\r') {
            if (pending_cr) {
                ++result.newline.cr_count;
                if (!add_checked(result.utf16_units, 1U)) return result;
            }
            pending_cr = true;
        } else if (scalar == U'\n') {
            if (pending_cr) ++result.newline.crlf_count;
            else ++result.newline.lf_count;
            pending_cr = false;
            if (!add_checked(result.utf16_units, 1U)) return result;
        } else {
            if (pending_cr) {
                ++result.newline.cr_count;
                if (!add_checked(result.utf16_units, 1U)) return result;
                pending_cr = false;
            }
            if (!add_checked(result.utf16_units, scalar_units(scalar))) return result;
        }
    }
    if (pending_cr) {
        ++result.newline.cr_count;
        if (!add_checked(result.utf16_units, 1U)) return result;
    }
    finish_newlines(result.newline);
    result.ok = true;
    return result;
}

bool decode(const std::span<const std::byte> input, const Analysis& analysis,
            const std::span<char16_t> output) noexcept {
    if (!analysis.ok || output.size() < analysis.utf16_units) return false;
    ScalarReader reader{input, analysis.encoding, bom_size(analysis.encoding)};
    std::size_t out = 0;
    bool pending_cr = false;
    while (reader.offset < input.size()) {
        char32_t scalar = 0;
        std::size_t error = 0;
        if (!reader.next(scalar, error)) return false;
        if (scalar == U'\r') {
            if (pending_cr && !put_utf16(U'\n', output, out)) return false;
            pending_cr = true;
        } else if (scalar == U'\n') {
            if (!put_utf16(U'\n', output, out)) return false;
            pending_cr = false;
        } else {
            if (pending_cr && !put_utf16(U'\n', output, out)) return false;
            pending_cr = false;
            if (!put_utf16(scalar, output, out)) return false;
        }
    }
    if (pending_cr && !put_utf16(U'\n', output, out)) return false;
    return out == analysis.utf16_units;
}

bool encoded_size(const std::span<const char16_t> input, const TextEncoding encoding,
                  const NewlineKind newline, std::size_t& size) noexcept {
    size = bom_size(encoding);
    std::size_t offset = 0;
    while (offset < input.size()) {
        char32_t scalar = 0;
        if (!next_utf16(input, offset, scalar)) return false;
        const std::size_t repeats = scalar == U'\n' && newline == NewlineKind::crlf ? 2U : 1U;
        char32_t output_scalar = scalar == U'\n' && newline != NewlineKind::lf ? U'\r' : scalar;
        for (std::size_t i = 0; i < repeats; ++i) {
            const char32_t current = i == 1U ? U'\n' : output_scalar;
            const std::size_t width = encoding == TextEncoding::utf16_le ||
                                      encoding == TextEncoding::utf16_be
                ? scalar_units(current) * 2U : utf8_width(current);
            if (width == 0 || !add_checked(size, width)) return false;
        }
    }
    return true;
}

bool encode(const std::span<const char16_t> input, const TextEncoding encoding,
            const NewlineKind newline, const std::span<std::byte> output,
            std::size_t& written) noexcept {
    written = 0;
    if (encoding == TextEncoding::utf8_bom) {
        if (output.size() < 3U) return false;
        output[written++] = std::byte{0xEF}; output[written++] = std::byte{0xBB};
        output[written++] = std::byte{0xBF};
    } else if (encoding == TextEncoding::utf16_le) {
        if (output.size() < 2U) return false;
        output[written++] = std::byte{0xFF}; output[written++] = std::byte{0xFE};
    } else if (encoding == TextEncoding::utf16_be) {
        if (output.size() < 2U) return false;
        output[written++] = std::byte{0xFE}; output[written++] = std::byte{0xFF};
    }
    std::size_t offset = 0;
    while (offset < input.size()) {
        char32_t scalar = 0;
        if (!next_utf16(input, offset, scalar)) return false;
        if (scalar == U'\n' && newline == NewlineKind::crlf) {
            if (!write_encoded_scalar(U'\r', encoding, output, written) ||
                !write_encoded_scalar(U'\n', encoding, output, written)) return false;
        } else if (scalar == U'\n' && newline == NewlineKind::cr) {
            if (!write_encoded_scalar(U'\r', encoding, output, written)) return false;
        } else if (!write_encoded_scalar(scalar, encoding, output, written)) {
            return false;
        }
    }
    return true;
}

bool contains_non_ascii(const std::span<const char16_t> input) noexcept {
    return std::any_of(input.begin(), input.end(), [](const char16_t value) {
        return value > 0x7FU;
    });
}

const wchar_t* encoding_name(const TextEncoding encoding) noexcept {
    switch (encoding) {
    case TextEncoding::ascii: return L"ASCII";
    case TextEncoding::utf8: return L"UTF-8";
    case TextEncoding::utf8_bom: return L"UTF-8 BOM";
    case TextEncoding::utf16_le: return L"UTF-16 LE";
    case TextEncoding::utf16_be: return L"UTF-16 BE";
    }
    return L"Unknown";
}

const wchar_t* newline_name(const NewlineKind newline) noexcept {
    switch (newline) {
    case NewlineKind::crlf: return L"CRLF";
    case NewlineKind::lf: return L"LF";
    case NewlineKind::cr: return L"CR";
    }
    return L"Unknown";
}

} // namespace mempad::encoding
