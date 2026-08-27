#pragma once

#include <windows.h>

namespace mempad::app::shell_integration {

struct Result final {
    bool succeeded = false;
    DWORD system_code = ERROR_SUCCESS;
    const wchar_t* extension = nullptr;
};

// Adds or updates the per-user Explorer verbs owned by mempad. Repeating the
// operation converges on the same registry values and is therefore safe.
Result install() noexcept;

// Removes only mempad's per-user Explorer verb subkeys. Missing keys count as
// success, so this operation is also safe to repeat.
Result uninstall() noexcept;

} // namespace mempad::app::shell_integration
