#include "app/application.h"
#include "security/process_guard.h"

#include <windows.h>
#include <cwchar>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
    if (!mempad::security::initialize_process_guard()) {
        if (std::wcsstr(GetCommandLineW(), L"--smoke-test") == nullptr) {
            MessageBoxW(nullptr,
                L"所需的 Windows 安全内存或错误报告 API 不可用，安全编辑器不会启动。",
                L"安全编辑器安全初始化失败", MB_OK | MB_ICONERROR);
        }
        return 1;
    }
    mempad::app::Application application;
    return application.run(instance, show_command);
}
