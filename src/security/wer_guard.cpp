#include "security/wer_guard.h"

#include <atomic>
#include <bit>
#include <cstdint>
#include <limits>

namespace mempad::security {

#ifdef _WIN32
namespace {
using RegisterFn = HRESULT(WINAPI*)(const void*, DWORD);
using UnregisterFn = HRESULT(WINAPI*)(const void*);
using SetFlagsFn = HRESULT(WINAPI*)(DWORD);

RegisterFn register_block = nullptr;
UnregisterFn unregister_block = nullptr;
SetFlagsFn set_flags = nullptr;
std::atomic<bool> initialized{false};
} // namespace

bool WerGuard::initialize() noexcept {
    if (initialized.load(std::memory_order_acquire)) {
        return true;
    }
    const HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    if (kernel == nullptr) {
        return false;
    }
    register_block = std::bit_cast<RegisterFn>(
        GetProcAddress(kernel, "WerRegisterExcludedMemoryBlock"));
    unregister_block = std::bit_cast<UnregisterFn>(
        GetProcAddress(kernel, "WerUnregisterExcludedMemoryBlock"));
    set_flags = std::bit_cast<SetFlagsFn>(GetProcAddress(kernel, "WerSetFlags"));
    if (register_block == nullptr || unregister_block == nullptr || set_flags == nullptr) {
        return false;
    }
    constexpr DWORD no_heap = 1U;
    if (FAILED(set_flags(no_heap))) {
        return false;
    }
    initialized.store(true, std::memory_order_release);
    return true;
}

bool WerGuard::ready() noexcept {
    return initialized.load(std::memory_order_acquire);
}

bool WerGuard::exclude(const void* address, const std::size_t size) noexcept {
    if (!ready() || address == nullptr || size == 0 ||
        size > static_cast<std::size_t>(std::numeric_limits<DWORD>::max())) {
        return false;
    }
    return SUCCEEDED(register_block(address, static_cast<DWORD>(size)));
}

void WerGuard::include(const void* address) noexcept {
    if (ready() && address != nullptr) {
        (void)unregister_block(address);
    }
}
#else
bool WerGuard::initialize() noexcept { return true; }
bool WerGuard::ready() noexcept { return true; }
bool WerGuard::exclude(const void*, std::size_t) noexcept { return true; }
void WerGuard::include(const void*) noexcept {}
#endif

} // namespace mempad::security
