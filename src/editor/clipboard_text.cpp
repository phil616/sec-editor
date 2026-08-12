#include "editor/clipboard_text.h"

#include "document/secure_gap_buffer.h"

namespace mempad::editor {

bool clipboard_text_normalized_size(const std::span<const char16_t> source,
                                    std::size_t& size) noexcept {
    size = 0;
    for (std::size_t index = 0; index < source.size(); ++index) {
        const char16_t value = source[index];
        if (document::is_high_surrogate(value)) {
            if (index + 1U >= source.size() ||
                !document::is_low_surrogate(source[index + 1U])) {
                return false;
            }
            size += 2U;
            ++index;
        } else if (document::is_low_surrogate(value)) {
            return false;
        } else {
            ++size;
            if (value == u'\r' && index + 1U < source.size() &&
                source[index + 1U] == u'\n') {
                ++index;
            }
        }
    }
    return true;
}

bool normalize_clipboard_text(const std::span<const char16_t> source,
                              const std::span<char16_t> destination) noexcept {
    std::size_t required = 0;
    if (!clipboard_text_normalized_size(source, required) ||
        destination.size() < required) {
        return false;
    }
    std::size_t output = 0;
    for (std::size_t index = 0; index < source.size(); ++index) {
        const char16_t value = source[index];
        if (value == u'\r') {
            destination[output++] = u'\n';
            if (index + 1U < source.size() && source[index + 1U] == u'\n') {
                ++index;
            }
        } else {
            destination[output++] = value;
        }
    }
    return output == required;
}

} // namespace mempad::editor
