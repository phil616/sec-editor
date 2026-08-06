#include "security/process_guard.h"

#include "security/wer_guard.h"

#include <atomic>
#include <cstdint>

namespace mempad::security {
namespace {
constexpr std::size_t region_count = 16;
struct CrashRegion {
    std::atomic<void*> pointer{nullptr};
    std::atomic<std::size_t> length{0};
};
CrashRegion regions[region_count];
} // namespace

void secure_zero(void* pointer, std::size_t length) noexcept {
    auto* current = static_cast<volatile unsigned char*>(pointer);
    while (length != 0) {
        *current++ = 0;
        --length;
    }
    std::atomic_signal_fence(std::memory_order_seq_cst);
}

bool register_crash_region(void* pointer, const std::size_t length) noexcept {
    if (pointer == nullptr || length == 0) {
        return false;
    }
    for (auto& region : regions) {
        void* expected = nullptr;
        if (region.pointer.compare_exchange_strong(expected, pointer,
                                                   std::memory_order_acq_rel)) {
            region.length.store(length, std::memory_order_release);
            return true;
        }
    }
    return false;
}

void unregister_crash_region(void* pointer) noexcept {
    for (auto& region : regions) {
        if (region.pointer.load(std::memory_order_acquire) == pointer) {
            region.length.store(0, std::memory_order_release);
            region.pointer.store(nullptr, std::memory_order_release);
            return;
        }
    }
}

void wipe_registered_regions() noexcept {
    for (auto& region : regions) {
        void* const pointer = region.pointer.load(std::memory_order_acquire);
        const std::size_t length = region.length.load(std::memory_order_acquire);
        if (pointer != nullptr && length != 0) {
            secure_zero(pointer, length);
        }
    }
}

#ifdef _WIN32
LONG WINAPI unhandled_exception_filter(EXCEPTION_POINTERS*) noexcept {
    wipe_registered_regions();
    TerminateProcess(GetCurrentProcess(), 0xDEADU);
    return EXCEPTION_EXECUTE_HANDLER;
}

bool initialize_process_guard() noexcept {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX |
                 SEM_NOOPENFILEERRORBOX);
    SetUnhandledExceptionFilter(unhandled_exception_filter);
    return WerGuard::initialize();
}
#endif

} // namespace mempad::security
