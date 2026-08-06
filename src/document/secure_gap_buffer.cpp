#include "document/secure_gap_buffer.h"

#include "security/process_guard.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace mempad::document {
namespace {
constexpr std::size_t initial_units = 2048;
}

bool is_high_surrogate(const char16_t value) noexcept {
    return value >= 0xD800U && value <= 0xDBFFU;
}
bool is_low_surrogate(const char16_t value) noexcept {
    return value >= 0xDC00U && value <= 0xDFFFU;
}

char16_t* SecureGapBuffer::storage() noexcept {
    return reinterpret_cast<char16_t*>(allocation_.data());
}
const char16_t* SecureGapBuffer::storage() const noexcept {
    return reinterpret_cast<const char16_t*>(allocation_.data());
}

std::size_t SecureGapBuffer::physical(const std::size_t logical) const noexcept {
    return logical < gap_begin_ ? logical : logical + (gap_end_ - gap_begin_);
}

bool SecureGapBuffer::ensure_capacity(const std::size_t required) noexcept {
    if (required > max_units) return false;
    if (capacity_ >= required && allocation_.valid()) return true;
    std::size_t requested = std::max(initial_units, capacity_);
    while (requested < required) {
        const std::size_t growth = std::max(requested / 2U, initial_units);
        if (requested > max_units - std::min(growth, max_units)) {
            requested = max_units;
            break;
        }
        requested = std::min(max_units, requested + growth);
    }
    security::SecureAllocation replacement;
    if (!replacement.allocate(requested * sizeof(char16_t))) return false;
    const std::size_t new_capacity = replacement.size() / sizeof(char16_t);
    if (new_capacity < required || new_capacity > max_units) return false;
    auto* target = reinterpret_cast<char16_t*>(replacement.data());
    const std::size_t suffix = document_length_ - gap_begin_;
    if (gap_begin_ != 0) {
        std::memcpy(target, storage(), gap_begin_ * sizeof(char16_t));
    }
    if (suffix != 0) {
        std::memcpy(target + new_capacity - suffix, storage() + gap_end_,
                    suffix * sizeof(char16_t));
    }
    allocation_ = std::move(replacement);
    capacity_ = new_capacity;
    gap_end_ = capacity_ - suffix;
    return true;
}

void SecureGapBuffer::move_gap(const std::size_t position) noexcept {
    if (position < gap_begin_) {
        const std::size_t distance = gap_begin_ - position;
        std::memmove(storage() + gap_end_ - distance, storage() + position,
                     distance * sizeof(char16_t));
        security::secure_zero(storage() + position, distance * sizeof(char16_t));
        gap_begin_ = position;
        gap_end_ -= distance;
    } else if (position > gap_begin_) {
        const std::size_t distance = position - gap_begin_;
        std::memmove(storage() + gap_begin_, storage() + gap_end_,
                     distance * sizeof(char16_t));
        security::secure_zero(storage() + gap_end_, distance * sizeof(char16_t));
        gap_begin_ += distance;
        gap_end_ += distance;
    }
}

bool SecureGapBuffer::assign(const std::span<const char16_t> text) noexcept {
    if (text.size() > max_units) return false;
    clear();
    if (!ensure_capacity(text.size())) return false;
    if (!text.empty()) {
        std::memcpy(storage(), text.data(), text.size_bytes());
    }
    gap_begin_ = text.size();
    gap_end_ = capacity_;
    document_length_ = text.size();
    return true;
}

bool SecureGapBuffer::insert(const std::size_t position,
                             const std::span<const char16_t> text) noexcept {
    if (position > document_length_ || text.size() > max_units - document_length_) {
        return false;
    }
    if (text.empty()) return true;
    if (!ensure_capacity(document_length_ + text.size())) return false;
    move_gap(position);
    std::memcpy(storage() + gap_begin_, text.data(), text.size_bytes());
    gap_begin_ += text.size();
    document_length_ += text.size();
    return true;
}

bool SecureGapBuffer::erase(const std::size_t begin, const std::size_t end) noexcept {
    if (begin > end || end > document_length_) return false;
    if (begin == end) return true;
    move_gap(begin);
    const std::size_t removed = end - begin;
    security::secure_zero(storage() + gap_end_, removed * sizeof(char16_t));
    gap_end_ += removed;
    document_length_ -= removed;
    return true;
}

bool SecureGapBuffer::replace(const std::size_t begin, const std::size_t end,
                              const std::span<const char16_t> text) noexcept {
    if (begin > end || end > document_length_ ||
        text.size() > max_units - (document_length_ - (end - begin))) return false;
    const std::size_t final_size = document_length_ - (end - begin) + text.size();
    if (!ensure_capacity(final_size)) return false;
    return erase(begin, end) && insert(begin, text);
}

char16_t SecureGapBuffer::at(const std::size_t position) const noexcept {
    if (position >= document_length_) return 0;
    return storage()[physical(position)];
}

void SecureGapBuffer::clear() noexcept {
    allocation_.release();
    gap_begin_ = 0;
    gap_end_ = 0;
    capacity_ = 0;
    document_length_ = 0;
}

bool SecureGapBuffer::copy_to(const std::span<char16_t> destination) const noexcept {
    return copy_range(0, document_length_, destination);
}

bool SecureGapBuffer::copy_range(const std::size_t begin, const std::size_t end,
                                 const std::span<char16_t> destination) const noexcept {
    if (begin > end || end > document_length_ || destination.size() < end - begin) {
        return false;
    }
    for (std::size_t i = begin; i < end; ++i) {
        destination[i - begin] = at(i);
    }
    return true;
}

std::size_t SecureGapBuffer::previous_scalar(std::size_t position) const noexcept {
    if (position == 0) return 0;
    --position;
    if (is_low_surrogate(at(position)) && position != 0 &&
        is_high_surrogate(at(position - 1U))) {
        --position;
    }
    return position;
}

std::size_t SecureGapBuffer::next_scalar(const std::size_t position) const noexcept {
    if (position >= document_length_) return document_length_;
    std::size_t next = position + 1U;
    if (is_high_surrogate(at(position)) && next < document_length_ &&
        is_low_surrogate(at(next))) {
        ++next;
    }
    return next;
}

} // namespace mempad::document
