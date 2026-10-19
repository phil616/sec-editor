#pragma once

#include "document/secure_gap_buffer.h"

#include <cstddef>

namespace mempad::editor {

struct TextRange final {
    std::size_t begin = 0;
    std::size_t end = 0;
};

// Scintilla-style runs: Unicode letters/marks/numbers/connector punctuation,
// horizontal whitespace, or punctuation/symbols. Never selects a line ending.
// position identifies the clicked character, rather than a nearest caret stop.
TextRange word_at(const document::SecureGapBuffer& text,
                  std::size_t position) noexcept;

struct SearchResult final {
    TextRange range{};
    std::size_t count = 0;
    std::size_t ordinal = 0; // one-based; zero means no match
    bool wrapped = false;
};

// Exact, case-sensitive UTF-16 literal search over the gap buffer, including
// overlapping matches. Forward includes origin; backward excludes origin.
// KMP: O(document + query) time, O(query) positional metadata, no text copies.
SearchResult find_literal(const document::SecureGapBuffer& text,
                          const document::SecureGapBuffer& query,
                          std::size_t origin, bool backwards) noexcept;

} // namespace mempad::editor
