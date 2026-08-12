#include "app/application.h"

#include "app/main_window.h"

#include <shellapi.h>
#include <objbase.h>
#include <string>

namespace mempad::app {
int Application::run(HINSTANCE instance, const int show_command) noexcept {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED |
                                               COINIT_DISABLE_OLE1DDE);
    if (FAILED(com)) return 10;
    int count = 0;
    wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (arguments == nullptr) {
        CoUninitialize();
        return 11;
    }
    if (count < 1 || count > 2 || (count == 2 && arguments[1][0] == L'\0')) {
        LocalFree(arguments);
        MessageBoxW(nullptr, L"用法：mempad.exe [filename]", L"mempad",
                    MB_OK | MB_ICONERROR);
        CoUninitialize();
        return 13;
    }
    std::wstring initial_path;
    if (count == 2) initial_path.assign(arguments[1]);
    LocalFree(arguments);

    MainWindow window(instance);
    if (!window.create(show_command, initial_path)) {
        MessageBoxW(nullptr,
            L"安全编辑器无法初始化安全文档内存或主窗口。",
            L"安全编辑器安全初始化失败", MB_OK | MB_ICONERROR);
        CoUninitialize();
        return 12;
    }
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    CoUninitialize();
    return static_cast<int>(message.wParam);
}

} // namespace mempad::app
