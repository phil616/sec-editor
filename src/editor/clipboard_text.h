#pragma once

#include <cstddef>
#include <span>

namespace mempad::editor {

bool clipboard_text_normalized_size(std::span<const char16_t> source,
                                    std::size_t& size) noexcept;
bool normalize_clipboard_text(std::span<const char16_t> source,
                              std::span<char16_t> destination) noexcept;

} // namespace mempad::editor
