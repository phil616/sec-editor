#pragma once

#include <cstddef>

#ifdef _WIN32
#include <windows.h>
#endif

namespace mempad::security {

void secure_zero(void* pointer, std::size_t length) noexcept;
bool register_crash_region(void* pointer, std::size_t length) noexcept;
void unregister_crash_region(void* pointer) noexcept;
void wipe_registered_regions() noexcept;

#ifdef _WIN32
LONG WINAPI unhandled_exception_filter(EXCEPTION_POINTERS*) noexcept;
bool initialize_process_guard() noexcept;
#else
inline bool initialize_process_guard() noexcept { return true; }
#endif

} // namespace mempad::security
