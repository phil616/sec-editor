#pragma once

#include "document/secure_gap_buffer.h"
#include "encoding/encoding.h"

#include <cstddef>
#include <span>
#include <string>

namespace mempad::document {

class SecureDocument final {
public:
    static constexpr std::size_t max_line_units = 1024U * 1024U;

    bool load(std::span<const char16_t> text, encoding::TextEncoding encoding,
              const encoding::NewlineInfo& newline, const std::wstring& path) noexcept;
    bool create_empty() noexcept;
    void close() noexcept;
    bool insert(std::size_t position, std::span<const char16_t> text) noexcept;
    bool erase(std::size_t begin, std::size_t end) noexcept;
    bool replace(std::size_t begin, std::size_t end,
                 std::span<const char16_t> text) noexcept;
    void mark_saved(const std::wstring& path, encoding::TextEncoding encoding) noexcept;

    const SecureGapBuffer& text() const noexcept { return text_; }
    SecureGapBuffer& text() noexcept { return text_; }
    bool open() const noexcept { return open_; }
    bool dirty() const noexcept { return dirty_; }
    bool secure() const noexcept { return open_ && text_.secure(); }
    const std::wstring& path() const noexcept { return path_; }
    encoding::TextEncoding encoding() const noexcept { return encoding_; }
    const encoding::NewlineInfo& newline() const noexcept { return newline_; }

private:
    bool line_limit_after(std::size_t begin, std::size_t end,
                          std::span<const char16_t> replacement) const noexcept;
    void update_ascii_state(std::span<const char16_t> inserted) noexcept;

    SecureGapBuffer text_{};
    std::wstring path_{};
    encoding::TextEncoding encoding_ = encoding::TextEncoding::utf8;
    encoding::NewlineInfo newline_{};
    bool open_ = false;
    bool dirty_ = false;
};

} // namespace mempad::document
