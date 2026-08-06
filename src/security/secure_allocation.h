#pragma once

#include <cstddef>

namespace mempad::security {

class SecureAllocation final {
public:
    SecureAllocation() noexcept = default;
    explicit SecureAllocation(std::size_t requested_size) noexcept;
    ~SecureAllocation();

    SecureAllocation(const SecureAllocation&) = delete;
    SecureAllocation& operator=(const SecureAllocation&) = delete;
    SecureAllocation(SecureAllocation&& other) noexcept;
    SecureAllocation& operator=(SecureAllocation&& other) noexcept;

    bool allocate(std::size_t requested_size) noexcept;
    void release() noexcept;
    std::byte* data() noexcept { return data_; }
    const std::byte* data() const noexcept { return data_; }
    std::size_t size() const noexcept { return data_size_; }
    bool valid() const noexcept { return data_ != nullptr; }

private:
    void move_from(SecureAllocation& other) noexcept;

    void* reservation_ = nullptr;
    std::byte* data_ = nullptr;
    std::size_t data_size_ = 0;
    std::size_t reservation_size_ = 0;
    bool virtual_locked_ = false;
    bool wer_excluded_ = false;
    bool crash_registered_ = false;
};

} // namespace mempad::security
