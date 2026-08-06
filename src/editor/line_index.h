#pragma once

#include "document/secure_gap_buffer.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mempad::editor {

class LineIndex final {
public:
    void rebuild(const document::SecureGapBuffer& text);
    std::size_t line_count() const noexcept { return starts_.size(); }
    std::size_t line_start(std::size_t line) const noexcept;
    std::size_t line_end(std::size_t line,
                         const document::SecureGapBuffer& text) const noexcept;
    std::size_t line_for_position(std::size_t position) const noexcept;
    std::size_t position(std::size_t line, std::size_t column,
                         const document::SecureGapBuffer& text) const noexcept;

private:
    std::vector<std::uint32_t> starts_{0};
};

} // namespace mempad::editor
