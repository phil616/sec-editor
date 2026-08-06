#include "document/secure_document.h"

#include <algorithm>

namespace mempad::document {

bool SecureDocument::load(const std::span<const char16_t> text,
                          const encoding::TextEncoding encoding,
                          const encoding::NewlineInfo& newline,
                          const std::wstring& path) noexcept {
    close();
    std::size_t line_length = 0;
    for (const char16_t value : text) {
        if (value == u'\n') line_length = 0;
        else if (++line_length > max_line_units) return false;
    }
    if (!text_.assign(text)) return false;
    encoding_ = encoding;
    newline_ = newline;
    path_ = path;
    open_ = true;
    dirty_ = false;
    return true;
}

bool SecureDocument::create_empty() noexcept {
    encoding::NewlineInfo newline{};
    return load({}, encoding::TextEncoding::utf8, newline, L"");
}

void SecureDocument::close() noexcept {
    text_.clear();
    path_.clear();
    encoding_ = encoding::TextEncoding::utf8;
    newline_ = {};
    open_ = false;
    dirty_ = false;
}

bool SecureDocument::line_limit_after(const std::size_t begin, const std::size_t end,
                                      const std::span<const char16_t> replacement) const noexcept {
    if (begin > end || end > text_.size()) return false;
    std::size_t prefix = 0;
    for (std::size_t pos = begin; pos != 0 && text_.at(pos - 1U) != u'\n'; --pos) ++prefix;
    std::size_t suffix = 0;
    for (std::size_t pos = end; pos < text_.size() && text_.at(pos) != u'\n'; ++pos) ++suffix;
    std::size_t current = prefix;
    bool saw_newline = false;
    for (const char16_t value : replacement) {
        if (value == u'\n') {
            if (current > max_line_units) return false;
            current = 0;
            saw_newline = true;
        } else if (++current > max_line_units) {
            return false;
        }
    }
    if (suffix > max_line_units - current) return false;
    (void)saw_newline;
    return true;
}

void SecureDocument::update_ascii_state(const std::span<const char16_t> inserted) noexcept {
    if (encoding_ == encoding::TextEncoding::ascii &&
        encoding::contains_non_ascii(inserted)) {
        encoding_ = encoding::TextEncoding::utf8;
    }
}

bool SecureDocument::insert(const std::size_t position,
                            const std::span<const char16_t> text) noexcept {
    if (!open_ || !line_limit_after(position, position, text) ||
        !text_.insert(position, text)) return false;
    update_ascii_state(text);
    dirty_ = true;
    return true;
}

bool SecureDocument::erase(const std::size_t begin, const std::size_t end) noexcept {
    if (!open_ || !text_.erase(begin, end)) return false;
    if (begin != end) dirty_ = true;
    return true;
}

bool SecureDocument::replace(const std::size_t begin, const std::size_t end,
                             const std::span<const char16_t> text) noexcept {
    if (!open_ || !line_limit_after(begin, end, text) ||
        !text_.replace(begin, end, text)) return false;
    update_ascii_state(text);
    if (begin != end || !text.empty()) dirty_ = true;
    return true;
}

void SecureDocument::mark_saved(const std::wstring& path,
                                const encoding::TextEncoding encoding) noexcept {
    path_ = path;
    encoding_ = encoding;
    dirty_ = false;
}

} // namespace mempad::document
