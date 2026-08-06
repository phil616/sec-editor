#include "app/application.h"

#include "app/main_window.h"
#include "io/file_reader.h"
#include "io/file_writer.h"

#include <shellapi.h>
#include <objbase.h>
#include <span>
#include <string>

namespace mempad::app {
namespace {
int smoke_test(const std::wstring& input, const std::wstring& output) noexcept {
    document::SecureDocument document;
    io::Error error{};
    if (!io::read_document(input, document, error)) return 20;
    constexpr char16_t marker[] = {u'\n', u'S', u'a', u'f', u'e', u'E', u'd', u'i',
                                   u't', u'o', u'r', u' ', u's', u'm', u'o', u'k', u'e'};
    if (!document.insert(document.text().size(), std::span<const char16_t>(marker))) return 21;
    if (!io::write_document(output, document, document.encoding(), error)) return 22;
    document.close();
    return 0;
}
} // namespace

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
    if (count == 4 && std::wstring(arguments[1]) == L"--smoke-test") {
        const int result = smoke_test(arguments[2], arguments[3]);
        LocalFree(arguments);
        CoUninitialize();
        return result;
    }
    std::wstring initial_path;
    if (count >= 2 && arguments[1][0] != L'-') initial_path.assign(arguments[1]);
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
