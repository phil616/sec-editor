#pragma once

#include <cstddef>

#ifdef _WIN32
#include <windows.h>
#endif

namespace mempad::security {

class WerGuard final {
public:
    static bool initialize() noexcept;
    static bool ready() noexcept;
    static bool exclude(const void* address, std::size_t size) noexcept;
    static void include(const void* address) noexcept;
};

} // namespace mempad::security
