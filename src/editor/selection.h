#pragma once

#include <algorithm>
#include <cstddef>

namespace mempad::editor {

class Selection final {
public:
    void collapse(std::size_t position) noexcept { anchor_ = caret_ = position; }
    void extend(std::size_t position) noexcept { caret_ = position; }
    void set(std::size_t anchor, std::size_t caret) noexcept { anchor_ = anchor; caret_ = caret; }
    std::size_t anchor() const noexcept { return anchor_; }
    std::size_t caret() const noexcept { return caret_; }
    std::size_t begin() const noexcept { return std::min(anchor_, caret_); }
    std::size_t end() const noexcept { return std::max(anchor_, caret_); }
    bool empty() const noexcept { return anchor_ == caret_; }

private:
    std::size_t anchor_ = 0;
    std::size_t caret_ = 0;
};

} // namespace mempad::editor
