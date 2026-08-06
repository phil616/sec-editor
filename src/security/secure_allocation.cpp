#include "security/secure_allocation.h"

#include "security/process_guard.h"
#include "security/wer_guard.h"

#include <algorithm>
#include <cstdint>
#include <limits>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace mempad::security {
namespace {
std::size_t page_size() noexcept {
#ifdef _WIN32
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    return static_cast<std::size_t>(info.dwPageSize);
#else
    const long value = sysconf(_SC_PAGESIZE);
    return value > 0 ? static_cast<std::size_t>(value) : 4096U;
#endif
}

bool round_up(const std::size_t value, const std::size_t alignment,
              std::size_t& result) noexcept {
    if (value > std::numeric_limits<std::size_t>::max() - (alignment - 1U)) {
        return false;
    }
    result = (value + alignment - 1U) & ~(alignment - 1U);
    return true;
}
} // namespace

SecureAllocation::SecureAllocation(const std::size_t requested_size) noexcept {
    (void)allocate(requested_size);
}

SecureAllocation::~SecureAllocation() { release(); }

SecureAllocation::SecureAllocation(SecureAllocation&& other) noexcept {
    move_from(other);
}

SecureAllocation& SecureAllocation::operator=(SecureAllocation&& other) noexcept {
    if (this != &other) {
        release();
        move_from(other);
    }
    return *this;
}

void SecureAllocation::move_from(SecureAllocation& other) noexcept {
    reservation_ = other.reservation_;
    data_ = other.data_;
    data_size_ = other.data_size_;
    reservation_size_ = other.reservation_size_;
    virtual_locked_ = other.virtual_locked_;
    wer_excluded_ = other.wer_excluded_;
    crash_registered_ = other.crash_registered_;
    other.reservation_ = nullptr;
    other.data_ = nullptr;
    other.data_size_ = 0;
    other.reservation_size_ = 0;
    other.virtual_locked_ = false;
    other.wer_excluded_ = false;
    other.crash_registered_ = false;
}

bool SecureAllocation::allocate(const std::size_t requested_size) noexcept {
    release();
    if (requested_size == 0 || !WerGuard::ready()) {
        return false;
    }
    const std::size_t page = page_size();
    std::size_t data_bytes = 0;
    if (!round_up(requested_size, page, data_bytes) ||
        data_bytes > std::numeric_limits<std::size_t>::max() - 2U * page) {
        return false;
    }
    const std::size_t total = data_bytes + 2U * page;
#ifdef _WIN32
    reservation_ = VirtualAlloc(nullptr, total, MEM_RESERVE, PAGE_NOACCESS);
    if (reservation_ == nullptr) {
        return false;
    }
    data_ = reinterpret_cast<std::byte*>(reservation_) + page;
    if (VirtualAlloc(data_, data_bytes, MEM_COMMIT, PAGE_READWRITE) == nullptr) {
        VirtualFree(reservation_, 0, MEM_RELEASE);
        reservation_ = nullptr;
        data_ = nullptr;
        return false;
    }
    data_size_ = data_bytes;
    reservation_size_ = total;
    if (VirtualLock(data_, data_bytes) == 0) {
        secure_zero(data_, data_bytes);
        VirtualFree(reservation_, 0, MEM_RELEASE);
        reservation_ = nullptr;
        data_ = nullptr;
        data_size_ = 0;
        reservation_size_ = 0;
        return false;
    }
    virtual_locked_ = true;
#else
    reservation_ = mmap(nullptr, total, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (reservation_ == MAP_FAILED) {
        reservation_ = nullptr;
        return false;
    }
    data_ = reinterpret_cast<std::byte*>(reservation_) + page;
    if (mprotect(data_, data_bytes, PROT_READ | PROT_WRITE) != 0) {
        munmap(reservation_, total);
        reservation_ = nullptr;
        data_ = nullptr;
        return false;
    }
    data_size_ = data_bytes;
    reservation_size_ = total;
    if (mlock(data_, data_bytes) == 0) {
        virtual_locked_ = true;
    }
#ifdef MADV_DONTDUMP
    if (madvise(data_, data_bytes, MADV_DONTDUMP) != 0) {
        release();
        return false;
    }
#endif
#endif
    if (!WerGuard::exclude(data_, data_bytes)) {
        release();
        return false;
    }
    wer_excluded_ = true;
    if (!register_crash_region(data_, data_bytes)) {
        release();
        return false;
    }
    crash_registered_ = true;
    return true;
}

void SecureAllocation::release() noexcept {
    if (data_ == nullptr) {
        return;
    }
    const std::size_t bytes = data_size_ != 0 ? data_size_ :
        reservation_size_ > 0 ? reservation_size_ - 2U * page_size() : 0;
    if (bytes != 0) {
        secure_zero(data_, bytes);
    }
    if (crash_registered_) {
        unregister_crash_region(data_);
    }
    if (wer_excluded_) {
        WerGuard::include(data_);
    }
#ifdef _WIN32
    if (virtual_locked_) {
        VirtualUnlock(data_, bytes);
    }
    if (reservation_ != nullptr) {
        VirtualFree(reservation_, 0, MEM_RELEASE);
    }
#else
    if (virtual_locked_) {
        munlock(data_, bytes);
    }
    if (reservation_ != nullptr && reservation_size_ != 0) {
        munmap(reservation_, reservation_size_);
    }
#endif
    reservation_ = nullptr;
    data_ = nullptr;
    data_size_ = 0;
    reservation_size_ = 0;
    virtual_locked_ = false;
    wer_excluded_ = false;
    crash_registered_ = false;
}

} // namespace mempad::security
