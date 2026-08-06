#pragma once

#include <windows.h>

namespace mempad::app {

class Application final {
public:
    int run(HINSTANCE instance, int show_command) noexcept;
};

} // namespace mempad::app
