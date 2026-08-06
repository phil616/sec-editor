#include "editor/line_index.h"

#include <algorithm>

namespace mempad::editor {

void LineIndex::rebuild(const document::SecureGapBuffer& text) {
    starts_.clear();
    starts_.push_back(0);
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text.at(i) == u'\n' && i + 1U <= UINT32_MAX) {
            starts_.push_back(static_cast<std::uint32_t>(i + 1U));
        }
    }
}

std::size_t LineIndex::line_start(const std::size_t line) const noexcept {
    return line < starts_.size() ? starts_[line] : starts_.back();
}

std::size_t LineIndex::line_end(const std::size_t line,
                                const document::SecureGapBuffer& text) const noexcept {
    if (line + 1U < starts_.size()) return starts_[line + 1U] - 1U;
    return text.size();
}

std::size_t LineIndex::line_for_position(const std::size_t position) const noexcept {
    const auto iterator = std::upper_bound(starts_.begin(), starts_.end(), position);
    return iterator == starts_.begin() ? 0U :
        static_cast<std::size_t>((iterator - starts_.begin()) - 1);
}

std::size_t LineIndex::position(const std::size_t line, const std::size_t column,
                                const document::SecureGapBuffer& text) const noexcept {
    const std::size_t bounded_line = std::min(line, starts_.size() - 1U);
    return std::min(line_start(bounded_line) + column, line_end(bounded_line, text));
}

} // namespace mempad::editor
