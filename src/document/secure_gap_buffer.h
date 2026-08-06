#pragma once

#include "security/secure_allocation.h"

#include <cstddef>
#include <span>

namespace mempad::document {

class SecureGapBuffer final {
public:
    static constexpr std::size_t max_document_bytes = 16U * 1024U * 1024U;
    static constexpr std::size_t max_units = max_document_bytes / sizeof(char16_t);

    bool assign(std::span<const char16_t> text) noexcept;
    bool insert(std::size_t position, std::span<const char16_t> text) noexcept;
    bool erase(std::size_t begin, std::size_t end) noexcept;
    bool replace(std::size_t begin, std::size_t end,
                 std::span<const char16_t> text) noexcept;
    char16_t at(std::size_t position) const noexcept;
    std::size_t size() const noexcept { return document_length_; }
    std::size_t capacity() const noexcept { return capacity_; }
    bool empty() const noexcept { return document_length_ == 0; }
    void clear() noexcept;
    bool copy_to(std::span<char16_t> destination) const noexcept;
    bool copy_range(std::size_t begin, std::size_t end,
                    std::span<char16_t> destination) const noexcept;
    bool secure() const noexcept { return allocation_.valid(); }

    std::size_t previous_scalar(std::size_t position) const noexcept;
    std::size_t next_scalar(std::size_t position) const noexcept;

private:
    bool ensure_capacity(std::size_t required) noexcept;
    void move_gap(std::size_t position) noexcept;
    char16_t* storage() noexcept;
    const char16_t* storage() const noexcept;
    std::size_t physical(std::size_t logical) const noexcept;

    security::SecureAllocation allocation_{};
    std::size_t gap_begin_ = 0;
    std::size_t gap_end_ = 0;
    std::size_t capacity_ = 0;
    std::size_t document_length_ = 0;
};

bool is_high_surrogate(char16_t value) noexcept;
bool is_low_surrogate(char16_t value) noexcept;

} // namespace mempad::document
