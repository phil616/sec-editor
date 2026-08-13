#include "editor/ini_env_highlight.h"

#include <cwchar>
#include <cwctype>

namespace mempad::editor::highlight {
namespace {

bool is_space(const char16_t value) noexcept {
    return value == u' ' || value == u'\t';
}

bool is_comment_marker(const char16_t value) noexcept {
    return value == u';' || value == u'#';
}

bool is_digit(const char16_t value) noexcept {
    return value >= u'0' && value <= u'9';
}

bool has_suffix(const std::wstring& path, const wchar_t* suffix) noexcept {
    const std::size_t suffix_length = std::wcslen(suffix);
    const std::size_t path_length = path.size();
    if (path_length < suffix_length) return false;
    const wchar_t* tail = path.data() + (path_length - suffix_length);
    for (std::size_t index = 0; index < suffix_length; ++index) {
        if (std::towlower(tail[index]) != std::towlower(suffix[index])) return false;
    }
    return true;
}

} // namespace

std::size_t tokenize_line(const document::SecureGapBuffer& text,
                          const std::size_t start, const std::size_t end,
                          const bool is_ini, Token* const tokens,
                          const std::size_t capacity) noexcept {
    if (tokens == nullptr || capacity == 0 || start >= end) return 0;
    std::size_t count = 0;
    const auto emit = [&](const std::size_t from, const std::size_t to,
                          const TokenKind kind) {
        if (from >= to || count >= capacity) return;
        tokens[count] = Token{from, to, kind};
        ++count;
    };

    // Split off everything after the value as whitespace plus an optional
    // trailing comment (" ; text", " # text").
    const auto emit_trailing = [&](const std::size_t from) {
        std::size_t marker = from;
        while (marker < end && is_space(text.at(marker))) ++marker;
        emit(from, marker, TokenKind::plain);
        if (marker < end && is_comment_marker(text.at(marker))) {
            emit(marker, end, TokenKind::comment);
        } else {
            emit(marker, end, TokenKind::plain);
        }
    };

    // Leading whitespace stays plain.
    std::size_t position = start;
    while (position < end && is_space(text.at(position))) ++position;
    emit(start, position, TokenKind::plain);
    if (position >= end) return count;

    const char16_t first = text.at(position);
    if (is_comment_marker(first)) {
        emit(position, end, TokenKind::comment);
        return count;
    }

    // [Section] headers only exist in .ini files.
    if (is_ini && first == u'[') {
        std::size_t close = position + 1U;
        while (close < end && text.at(close) != u']') ++close;
        if (close >= end) {
            emit(position, end, TokenKind::plain);
            return count;
        }
        emit(position, close + 1U, TokenKind::section);
        emit_trailing(close + 1U);
        return count;
    }

    // `key = value`; ':' also separates in .ini but never in .env.
    std::size_t separator = position;
    while (separator < end) {
        const char16_t value = text.at(separator);
        if (value == u'=' || (is_ini && value == u':')) break;
        ++separator;
    }
    if (separator >= end) {
        emit(position, end, TokenKind::plain);
        return count;
    }

    // Key token, trailing whitespace before the separator stays plain.
    std::size_t key_end = separator;
    while (key_end > position && is_space(text.at(key_end - 1U))) --key_end;
    emit(position, key_end, TokenKind::key);
    emit(key_end, separator, TokenKind::plain);
    emit(separator, separator + 1U, TokenKind::operator_);

    // Value: quoted string, bare number, or plain text.
    const std::size_t value_start = separator + 1U;
    std::size_t probe = value_start;
    while (probe < end && is_space(text.at(probe))) ++probe;
    if (probe < end && (text.at(probe) == u'"' || text.at(probe) == u'\'')) {
        const char16_t quote = text.at(probe);
        std::size_t close = probe + 1U;
        while (close < end && text.at(close) != quote) ++close;
        const std::size_t value_end = close < end ? close + 1U : end;
        emit(value_start, probe, TokenKind::plain);
        emit(probe, value_end, TokenKind::string_);
        emit_trailing(value_end);
        return count;
    }
    std::size_t digits = probe;
    while (digits < end && is_digit(text.at(digits))) ++digits;
    if (digits < end && text.at(digits) == u'.') {
        ++digits;
        while (digits < end && is_digit(text.at(digits))) ++digits;
    }
    bool numeric = digits > probe;
    if (numeric && digits < end) {
        numeric = is_space(text.at(digits)) || is_comment_marker(text.at(digits));
    }
    if (numeric) {
        emit(value_start, probe, TokenKind::plain);
        emit(probe, digits, TokenKind::number);
        emit_trailing(digits);
        return count;
    }
    emit(value_start, end, TokenKind::plain);
    return count;
}

bool path_is_highlightable(const std::wstring& path) noexcept {
    return has_suffix(path, L".ini") || has_suffix(path, L".env");
}

bool path_is_ini(const std::wstring& path) noexcept {
    return has_suffix(path, L".ini");
}

} // namespace mempad::editor::highlight
