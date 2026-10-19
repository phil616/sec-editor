#include "editor/text_navigation.h"

#include "editor/word_ranges.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace mempad::editor {
namespace {
enum class CharacterKind { word, space, punctuation, newline };

CharacterKind kind_at(const document::SecureGapBuffer& text,
                      const std::size_t position) noexcept {
    std::uint32_t scalar = text.at(position);
    if (document::is_high_surrogate(static_cast<char16_t>(scalar)) &&
        position + 1U < text.size() &&
        document::is_low_surrogate(text.at(position + 1U))) {
        scalar = 0x10000U + ((scalar - 0xD800U) << 10U) +
                 (static_cast<std::uint32_t>(text.at(position + 1U)) - 0xDC00U);
    }
    if (scalar == '\n' || scalar == '\r' || scalar == 0x85U ||
        scalar == 0x2028U || scalar == 0x2029U) return CharacterKind::newline;
    if (scalar <= 0x20U || scalar == 0xA0U || scalar == 0x1680U ||
        (scalar >= 0x2000U && scalar <= 0x200AU) || scalar == 0x202FU ||
        scalar == 0x205FU || scalar == 0x3000U) return CharacterKind::space;
    const auto found = std::lower_bound(
        std::begin(word_ranges), std::end(word_ranges), scalar,
        [](const ScalarRange& range, const std::uint32_t value) {
            return range.last < value;
        });
    return found != std::end(word_ranges) && found->first <= scalar
        ? CharacterKind::word : CharacterKind::punctuation;
}
}

TextRange word_at(const document::SecureGapBuffer& text,
                  std::size_t position) noexcept {
    position = std::min(position, text.size());
    if (position == text.size()) return {position, position};
    if (position != 0 && document::is_low_surrogate(text.at(position)) &&
        document::is_high_surrogate(text.at(position - 1U))) --position;
    const CharacterKind kind = kind_at(text, position);
    if (kind == CharacterKind::newline) return {position, position};
    TextRange range{position, text.next_scalar(position)};
    while (range.begin != 0) {
        const std::size_t previous = text.previous_scalar(range.begin);
        if (kind_at(text, previous) != kind) break;
        range.begin = previous;
    }
    while (range.end < text.size() && kind_at(text, range.end) == kind) {
        range.end = text.next_scalar(range.end);
    }
    return range;
}

SearchResult find_literal(const document::SecureGapBuffer& text,
                          const document::SecureGapBuffer& query,
                          const std::size_t origin, const bool backwards) noexcept {
    SearchResult result{};
    if (query.empty() || query.size() > text.size()) return result;
    std::vector<std::size_t> prefix(query.size(), 0);
    for (std::size_t index = 1, matched = 0; index < query.size(); ++index) {
        while (matched != 0 && query.at(index) != query.at(matched)) {
            matched = prefix[matched - 1U];
        }
        if (query.at(index) == query.at(matched)) ++matched;
        prefix[index] = matched;
    }
    TextRange first{}, last{};
    for (std::size_t index = 0, matched = 0; index < text.size(); ++index) {
        while (matched != 0 && text.at(index) != query.at(matched)) {
            matched = prefix[matched - 1U];
        }
        if (text.at(index) == query.at(matched)) ++matched;
        if (matched != query.size()) continue;
        const TextRange range{index + 1U - matched, index + 1U};
        ++result.count;
        if (result.count == 1) first = range;
        last = range;
        if ((backwards && range.begin < origin) ||
            (!backwards && result.ordinal == 0 && range.begin >= origin)) {
            result.range = range;
            result.ordinal = result.count;
        }
        matched = prefix[matched - 1U];
    }
    if (result.count != 0 && result.ordinal == 0) {
        result.range = backwards ? last : first;
        result.ordinal = backwards ? result.count : 1U;
        result.wrapped = true;
    }
    return result;
}

} // namespace mempad::editor
