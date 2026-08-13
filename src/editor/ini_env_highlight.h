#pragma once

#include "document/secure_gap_buffer.h"

#include <cstddef>
#include <string>

namespace mempad::editor::highlight {

enum class TokenKind {
    plain,
    comment,
    section,
    key,
    operator_,
    string_,
    number,
};

struct Token final {
    std::size_t begin = 0;
    std::size_t end = 0;
    TokenKind kind = TokenKind::plain;
};

// Splits the text range [start, end) of one logical line into at most
// `capacity` tokens and returns the number of tokens written. Every position
// covered by the range belongs to exactly one emitted token; if `capacity` is
// exhausted the remaining text is simply not tokenized and the caller can
// render it with the plain color.
//
// Supported syntax (deliberately simple):
//   .ini: `;`/`#` comments, `[section]` headers, `key = value` / `key: value`,
//         quoted string values, bare numeric values, trailing comments.
//   .env: `#`/`;` comments, `key=value` (':' never splits), quoted string
//         values, bare numeric values, trailing comments.
std::size_t tokenize_line(const document::SecureGapBuffer& text,
                          std::size_t start, std::size_t end, bool is_ini,
                          Token* tokens, std::size_t capacity) noexcept;

// True when `path` ends in ".ini" or ".env", case-insensitively.
bool path_is_highlightable(const std::wstring& path) noexcept;

// True when `path` ends in ".ini" (case-insensitive), so section headers
// `[name]` are recognized.
bool path_is_ini(const std::wstring& path) noexcept;

} // namespace mempad::editor::highlight
