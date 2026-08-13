#include "app/main_window.h"

#include "app/commands.h"
#include "io/file_dialog.h"
#include "io/file_reader.h"
#include "io/file_writer.h"

#include <algorithm>
#include <cwchar>
#include <dwmapi.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <windowsx.h>
#include <wtsapi32.h>

namespace mempad::app {
namespace {
constexpr wchar_t class_name[] = L"SafeEditorMainWindow";
constexpr UINT editor_changed = WM_APP + 1U;
// WDA_EXCLUDEFROMCAPTURE is available in newer SDK headers, but the documented
// value is also accepted by older supported Windows 10 releases as WDA_MONITOR.
constexpr DWORD screen_capture_exclusion_affinity = 0x00000011U;

bool system_uses_dark_theme() noexcept {
    HIGHCONTRASTW high_contrast{};
    high_contrast.cbSize = sizeof(high_contrast);
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(high_contrast),
                              &high_contrast, 0) != 0 &&
        (high_contrast.dwFlags & HCF_HIGHCONTRASTON) != 0) {
        return false;
    }
    DWORD use_light = 1;
    DWORD size = sizeof(use_light);
    const LSTATUS result = RegGetValueW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &use_light, &size);
    return result == ERROR_SUCCESS && use_light == 0;
}
}

MainWindow::MainWindow(HINSTANCE instance) noexcept
    : instance_(instance), editor_(document_) {}

MainWindow::~MainWindow() {
    document_.close();
}

bool MainWindow::create(const int show_command,
                        const std::wstring& initial_path) noexcept {
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = instance_;
    window_class.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
    window_class.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(1));
    window_class.hIconSm = window_class.hIcon;
    window_class.lpszClassName = class_name;
    if (RegisterClassExW(&window_class) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }
    window_ = CreateWindowExW(WS_EX_ACCEPTFILES, class_name, L"安全编辑器",
                              WS_OVERLAPPEDWINDOW | WS_VSCROLL | WS_HSCROLL,
                              CW_USEDEFAULT, CW_USEDEFAULT, 900, 650, nullptr, nullptr,
                              instance_, this);
    if (window_ == nullptr) return false;
    DragAcceptFiles(window_, TRUE);
    apply_theme();
    ShowWindow(window_, show_command);
    UpdateWindow(window_);
    if (!initial_path.empty()) open_path(initial_path);
    return true;
}

LRESULT CALLBACK MainWindow::window_proc(HWND window, const UINT message,
                                         const WPARAM wparam,
                                         const LPARAM lparam) noexcept {
    MainWindow* self = reinterpret_cast<MainWindow*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        self = static_cast<MainWindow*>(create->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self != nullptr ? self->dispatch(message, wparam, lparam)
                           : DefWindowProcW(window, message, wparam, lparam);
}

bool MainWindow::on_create() noexcept {
    if (!create_menu_bar()) return false;
    if (!editor_.initialize(window_)) return false;
    if (!document_.create_empty()) return false;
    editor_.document_changed();
    editor_.set_tab_mode(tab_mode_);
    editor_.set_font(font_preset_);
    editor_.set_syntax_highlighting(syntax_highlighting_);
    update_menu_checks();
    update_security_menu();
    if (WTSRegisterSessionNotification(window_, NOTIFY_FOR_THIS_SESSION) == FALSE) {
        return false;
    }
    return true;
}

bool MainWindow::create_menu_bar() noexcept {
    menu_bar_ = CreateMenu();
    HMENU file_menu = CreatePopupMenu();
    HMENU edit_menu = CreatePopupMenu();
    tab_menu_ = CreatePopupMenu();
    theme_menu_ = CreatePopupMenu();
    format_menu_ = CreatePopupMenu();
    font_menu_ = CreatePopupMenu();
    security_menu_ = CreatePopupMenu();
    HMENU help_menu = CreatePopupMenu();
    if (menu_bar_ == nullptr || file_menu == nullptr || edit_menu == nullptr ||
        tab_menu_ == nullptr ||
        theme_menu_ == nullptr || format_menu_ == nullptr ||
        font_menu_ == nullptr ||
        security_menu_ == nullptr || help_menu == nullptr) {
        if (menu_bar_ != nullptr) DestroyMenu(menu_bar_);
        if (file_menu != nullptr) DestroyMenu(file_menu);
        if (edit_menu != nullptr) DestroyMenu(edit_menu);
        if (tab_menu_ != nullptr) DestroyMenu(tab_menu_);
        if (theme_menu_ != nullptr) DestroyMenu(theme_menu_);
        if (format_menu_ != nullptr) DestroyMenu(format_menu_);
        if (font_menu_ != nullptr) DestroyMenu(font_menu_);
        if (security_menu_ != nullptr) DestroyMenu(security_menu_);
        if (help_menu != nullptr) DestroyMenu(help_menu);
        menu_bar_ = security_menu_ = tab_menu_ = theme_menu_ = nullptr;
        format_menu_ = font_menu_ = nullptr;
        return false;
    }

    AppendMenuW(file_menu, MF_STRING, command_open, L"打开(&O)...\tCtrl+O");
    AppendMenuW(file_menu, MF_STRING, command_save, L"保存(&S)\tCtrl+S");
    AppendMenuW(file_menu, MF_STRING, command_save_as, L"另存为(&A)...");
    AppendMenuW(file_menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file_menu, MF_STRING, command_close, L"关闭文档(&C)");
    AppendMenuW(file_menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file_menu, MF_STRING, command_exit, L"退出(&X)");

    AppendMenuW(edit_menu, MF_STRING, command_cut, L"剪切选中(&T)\tCtrl+X");
    AppendMenuW(edit_menu, MF_STRING, command_copy, L"复制选中(&C)\tCtrl+C");
    AppendMenuW(edit_menu, MF_STRING, command_paste, L"粘贴(&P)\tCtrl+V");
    AppendMenuW(edit_menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(edit_menu, MF_STRING, command_select_all, L"全选(&A)\tCtrl+A");

    AppendMenuW(tab_menu_, MF_STRING, command_tab_literal,
                L"插入制表符（\\t，显示宽度 4）");
    AppendMenuW(tab_menu_, MF_STRING, command_tab_spaces, L"插入四个空格");

    AppendMenuW(theme_menu_, MF_STRING, command_theme_light, L"浅色(&L)");
    AppendMenuW(theme_menu_, MF_STRING, command_theme_dark, L"深色(&D)");
    AppendMenuW(theme_menu_, MF_STRING, command_theme_system, L"跟随系统(&S)");

    AppendMenuW(font_menu_, MF_STRING, command_font_consolas_dengxian,
                L"Consolas + 等线（默认）(&C)");
    AppendMenuW(font_menu_, MF_STRING, command_font_consolas_simhei,
                L"Consolas + 黑体(&H)");
    AppendMenuW(font_menu_, MF_STRING, command_font_dengxian, L"等线 DengXian(&D)");
    AppendMenuW(font_menu_, MF_STRING, command_font_simhei, L"黑体 SimHei(&S)");
    AppendMenuW(format_menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(font_menu_),
                L"字体(&F)");
    AppendMenuW(format_menu_, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(format_menu_, MF_STRING, command_syntax_highlight,
                L"颜色渲染（.ini / .env 语法高亮）(&R)");

    AppendMenuW(security_menu_, MF_STRING | MF_GRAYED,
                command_security_status, L"当前文档：正在检查");
    AppendMenuW(security_menu_, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(security_menu_, MF_STRING, command_screen_capture_protection,
                L"阻止屏幕捕获(&P)");
    AppendMenuW(security_menu_, MF_STRING | MF_GRAYED, 0,
                L"仅在本次运行中生效，默认关闭");
    AppendMenuW(security_menu_, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(security_menu_, MF_STRING | MF_GRAYED, 0,
                L"正文内存使用 VirtualLock 并排除 WER");

    AppendMenuW(help_menu, MF_STRING, command_about, L"关于安全编辑器(&A)...");

    AppendMenuW(menu_bar_, MF_POPUP, reinterpret_cast<UINT_PTR>(file_menu), L"文件(&F)");
    AppendMenuW(menu_bar_, MF_POPUP, reinterpret_cast<UINT_PTR>(edit_menu), L"编辑(&E)");
    AppendMenuW(menu_bar_, MF_POPUP, reinterpret_cast<UINT_PTR>(tab_menu_), L"Tab(&T)");
    AppendMenuW(menu_bar_, MF_POPUP, reinterpret_cast<UINT_PTR>(theme_menu_), L"主题(&V)");
    AppendMenuW(menu_bar_, MF_POPUP, reinterpret_cast<UINT_PTR>(format_menu_), L"格式(&O)");
    AppendMenuW(menu_bar_, MF_POPUP, reinterpret_cast<UINT_PTR>(security_menu_),
                L"安全状态：正在检查(&S)");
    AppendMenuW(menu_bar_, MF_POPUP, reinterpret_cast<UINT_PTR>(help_menu), L"帮助(&H)");
    if (SetMenu(window_, menu_bar_) == FALSE) return false;
    return true;
}

LRESULT MainWindow::dispatch(const UINT message, const WPARAM wparam,
                             const LPARAM lparam) noexcept {
    switch (message) {
    case WM_CREATE:
        return on_create() ? 0 : -1;
    case WM_COMMAND:
        on_command(LOWORD(wparam));
        return 0;
    case WM_KEYDOWN: {
        const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        if (control && wparam == 'O') { open_dialog(); return 0; }
        if (control && wparam == 'S') { (void)save(false); return 0; }
        editor_.key_down(wparam, (GetKeyState(VK_SHIFT) & 0x8000) != 0, control);
        return 0;
    }
    case WM_CHAR:
        editor_.character(static_cast<char16_t>(wparam)); return 0;
    case WM_COPY:
        editor_.copy_selection_to_clipboard(); return 0;
    case WM_CUT:
        editor_.cut_selection_to_clipboard(); return 0;
    case WM_PASTE:
        editor_.paste_from_clipboard(); return 0;
    case WM_PAINT:
        editor_.paint(); return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        editor_.resize(LOWORD(lparam), HIWORD(lparam)); return 0;
    case WM_LBUTTONDOWN:
        editor_.mouse_down(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam),
                           (wparam & MK_SHIFT) != 0); return 0;
    case WM_MOUSEMOVE:
        editor_.mouse_move(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam),
                           (wparam & MK_LBUTTON) != 0); return 0;
    case WM_LBUTTONUP:
        editor_.mouse_up(); return 0;
    case WM_VSCROLL:
        editor_.vertical_scroll(wparam); return 0;
    case WM_HSCROLL:
        editor_.horizontal_scroll(wparam); return 0;
    case WM_MOUSEWHEEL:
        editor_.mouse_wheel(GET_WHEEL_DELTA_WPARAM(wparam)); return 0;
    case WM_DROPFILES: {
        const HDROP drop = reinterpret_cast<HDROP>(wparam);
        const UINT count = DragQueryFileW(drop, 0xFFFFFFFFU, nullptr, 0);
        if (count != 0) {
            const UINT length = DragQueryFileW(drop, 0, nullptr, 0);
            std::wstring path(static_cast<std::size_t>(length) + 1U, L'\0');
            if (DragQueryFileW(drop, 0, path.data(), length + 1U) != 0) {
                path.resize(length);
                open_path(path);
            }
        }
        DragFinish(drop);
        return 0;
    }
    case WM_SETFOCUS:
        editor_.focus(true); return 0;
    case WM_KILLFOCUS:
        editor_.focus(false); return 0;
    case WM_SETTINGCHANGE:
        if (theme_preference_ == ThemePreference::system) apply_theme();
        else InvalidateRect(window_, nullptr, FALSE);
        return DefWindowProcW(window_, message, wparam, lparam);
    case WM_THEMECHANGED:
    case WM_SYSCOLORCHANGE:
        InvalidateRect(window_, nullptr, FALSE);
        return DefWindowProcW(window_, message, wparam, lparam);
    case editor_changed:
        update_title(); return 0;
    case WM_CLOSE:
        if (confirm_close_document() != CloseDecision::cancelled) {
            DestroyWindow(window_);
        }
        return 0;
    case WM_QUERYENDSESSION:
        shutting_down_ = true;
        document_.close();
        editor_.document_changed();
        return TRUE;
    case WM_ENDSESSION:
        if (wparam != 0) DestroyWindow(window_);
        return 0;
    case WM_POWERBROADCAST:
        if (wparam == PBT_APMQUERYSUSPEND || wparam == PBT_APMSUSPEND) {
            fail_closed(L"suspend");
            return TRUE;
        }
        if (wparam == PBT_APMRESUMEAUTOMATIC || wparam == PBT_APMRESUMESUSPEND) {
            show_pending_clear_notice();
        }
        return TRUE;
    case WM_WTSSESSION_CHANGE:
        if (wparam == WTS_SESSION_LOCK) fail_closed(L"session lock");
        else if (wparam == WTS_SESSION_UNLOCK) show_pending_clear_notice();
        return 0;
    case WM_DESTROY:
        DragAcceptFiles(window_, FALSE);
        WTSUnRegisterSessionNotification(window_);
        editor_.shutdown();
        document_.close();
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window_, message, wparam, lparam);
    }
}

void MainWindow::on_command(const unsigned command) noexcept {
    switch (command) {
    case command_open: open_dialog(); break;
    case command_save: (void)save(false); break;
    case command_save_as: (void)save(true); break;
    case command_close: close_document(); break;
    case command_exit: SendMessageW(window_, WM_CLOSE, 0, 0); break;
    case command_cut: editor_.cut_selection_to_clipboard(); break;
    case command_copy: editor_.copy_selection_to_clipboard(); break;
    case command_paste: editor_.paste_from_clipboard(); break;
    case command_select_all: editor_.select_all(); break;
    case command_tab_literal:
        tab_mode_ = editor::TabMode::literal_tab;
        editor_.set_tab_mode(tab_mode_);
        update_menu_checks();
        break;
    case command_tab_spaces:
        tab_mode_ = editor::TabMode::four_spaces;
        editor_.set_tab_mode(tab_mode_);
        update_menu_checks();
        break;
    case command_theme_light:
        theme_preference_ = ThemePreference::light;
        update_menu_checks();
        apply_theme();
        break;
    case command_theme_dark:
        theme_preference_ = ThemePreference::dark;
        update_menu_checks();
        apply_theme();
        break;
    case command_theme_system:
        theme_preference_ = ThemePreference::system;
        update_menu_checks();
        apply_theme();
        break;
    case command_font_consolas_dengxian:
        font_preset_ = editor::FontPreset::consolas_dengxian;
        editor_.set_font(font_preset_);
        update_menu_checks();
        break;
    case command_font_consolas_simhei:
        font_preset_ = editor::FontPreset::consolas_simhei;
        editor_.set_font(font_preset_);
        update_menu_checks();
        break;
    case command_font_dengxian:
        font_preset_ = editor::FontPreset::dengxian;
        editor_.set_font(font_preset_);
        update_menu_checks();
        break;
    case command_font_simhei:
        font_preset_ = editor::FontPreset::simhei;
        editor_.set_font(font_preset_);
        update_menu_checks();
        break;
    case command_syntax_highlight:
        syntax_highlighting_ = !syntax_highlighting_;
        editor_.set_syntax_highlighting(syntax_highlighting_);
        update_menu_checks();
        break;
    case command_screen_capture_protection:
        toggle_screen_capture_protection();
        break;
    case command_about: show_about(); break;
    case command_security_status: break;
    default: break;
    }
}

void MainWindow::apply_theme() noexcept {
    if (window_ == nullptr) return;
    const bool dark = theme_preference_ == ThemePreference::dark ||
        (theme_preference_ == ThemePreference::system && system_uses_dark_theme());
    const BOOL dark_mode = dark ? TRUE : FALSE;
    (void)DwmSetWindowAttribute(window_, static_cast<DWMWINDOWATTRIBUTE>(20),
                                &dark_mode, sizeof(dark_mode));
    (void)SetWindowTheme(window_, dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    editor_.set_dark_theme(dark);
    RedrawWindow(window_, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_NOERASE | RDW_FRAME | RDW_ALLCHILDREN);
    DrawMenuBar(window_);
}

void MainWindow::update_menu_checks() noexcept {
    if (tab_menu_ != nullptr) {
        const UINT selected = tab_mode_ == editor::TabMode::literal_tab
            ? command_tab_literal : command_tab_spaces;
        CheckMenuRadioItem(tab_menu_, command_tab_literal, command_tab_spaces,
                           selected, MF_BYCOMMAND);
    }
    if (theme_menu_ != nullptr) {
        UINT selected = command_theme_system;
        if (theme_preference_ == ThemePreference::light) selected = command_theme_light;
        else if (theme_preference_ == ThemePreference::dark) selected = command_theme_dark;
        CheckMenuRadioItem(theme_menu_, command_theme_light, command_theme_system,
                           selected, MF_BYCOMMAND);
    }
    if (font_menu_ != nullptr) {
        UINT selected = command_font_consolas_dengxian;
        switch (font_preset_) {
        case editor::FontPreset::consolas_dengxian: break;
        case editor::FontPreset::consolas_simhei:
            selected = command_font_consolas_simhei; break;
        case editor::FontPreset::dengxian:
            selected = command_font_dengxian; break;
        case editor::FontPreset::simhei:
            selected = command_font_simhei; break;
        }
        CheckMenuRadioItem(font_menu_, command_font_consolas_dengxian,
                           command_font_simhei, selected, MF_BYCOMMAND);
    }
    if (format_menu_ != nullptr) {
        CheckMenuItem(format_menu_, command_syntax_highlight,
                      MF_BYCOMMAND | (syntax_highlighting_
                          ? MF_CHECKED : MF_UNCHECKED));
    }
    if (security_menu_ != nullptr) {
        CheckMenuItem(security_menu_, command_screen_capture_protection,
                      MF_BYCOMMAND | (screen_capture_protection_enabled_
                          ? MF_CHECKED : MF_UNCHECKED));
    }
    if (window_ != nullptr) DrawMenuBar(window_);
}

void MainWindow::update_security_menu() noexcept {
    if (menu_bar_ == nullptr || security_menu_ == nullptr) return;
    const wchar_t* top_label = L"安全状态：无文档(&S)";
    const wchar_t* item_label = L"当前文档：未打开";
    bool safe = false;
    if (document_.open()) {
        safe = document_.secure();
        top_label = safe ? L"安全状态：安全(&S)" : L"安全状态：不安全(&S)";
        item_label = safe ? L"当前文档：安全" : L"当前文档：不安全";
    }
    ModifyMenuW(security_menu_, command_security_status,
                MF_BYCOMMAND | MF_STRING | MF_GRAYED,
                command_security_status, item_label);
    CheckMenuItem(security_menu_, command_security_status,
                  MF_BYCOMMAND | (safe ? MF_CHECKED : MF_UNCHECKED));
    const int menu_count = GetMenuItemCount(menu_bar_);
    for (int position = 0; position < menu_count; ++position) {
        if (GetSubMenu(menu_bar_, position) != security_menu_) continue;
        MENUITEMINFOW item{};
        item.cbSize = sizeof(item);
        item.fMask = MIIM_STRING;
        item.dwTypeData = const_cast<wchar_t*>(top_label);
        (void)SetMenuItemInfoW(menu_bar_, static_cast<UINT>(position), TRUE, &item);
        break;
    }
    DrawMenuBar(window_);
}

void MainWindow::toggle_screen_capture_protection() noexcept {
    const bool enable = !screen_capture_protection_enabled_;
    const DWORD affinity = enable ? screen_capture_exclusion_affinity : WDA_NONE;
    SetLastError(ERROR_SUCCESS);
    if (SetWindowDisplayAffinity(window_, affinity) == FALSE) {
        const DWORD error = GetLastError();
        wchar_t system_message[192]{};
        if (error != ERROR_SUCCESS) {
            (void)FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM |
                                     FORMAT_MESSAGE_IGNORE_INSERTS,
                                 nullptr, error, 0, system_message,
                                 static_cast<DWORD>(std::size(system_message)), nullptr);
        }
        wchar_t message[512]{};
        std::swprintf(message, std::size(message),
                      L"无法%ls屏幕捕获保护。当前设置未改变。\n\n"
                      L"Windows 错误码：%lu\n%ls",
                      enable ? L"启用" : L"关闭",
                      static_cast<unsigned long>(error), system_message);
        MessageBoxW(window_, message, L"安全编辑器", MB_OK | MB_ICONERROR);
        return;
    }
    screen_capture_protection_enabled_ = enable;
    update_menu_checks();
}

void MainWindow::show_about() noexcept {
    MessageBoxW(window_,
        L"安全编辑器\n"
        L"版本：v" SAFEEDITOR_VERSION L"\n\n"
        L"作者：GreenShadeCapitalSecTeam\n"
        L"版权所有 © 2026 GreenShadeCapitalSecTeam\n"
        L"许可证：MIT License\n\n"
        L"用于敏感纯文本的原生 Windows 安全内存编辑器。\n",
        L"关于安全编辑器", MB_OK | MB_ICONINFORMATION);
}

MainWindow::CloseDecision MainWindow::confirm_close_document() noexcept {
    if (!document_.open() || !document_.dirty() || shutting_down_) {
        return CloseDecision::proceed;
    }
    const int answer = MessageBoxW(window_,
        L"文档有未保存的修改。是否先保存？",
        L"安全编辑器", MB_YESNOCANCEL | MB_ICONWARNING);
    if (answer == IDCANCEL) return CloseDecision::cancelled;
    if (answer == IDYES) {
        return save(false) ? CloseDecision::proceed : CloseDecision::cancelled;
    }
    return answer == IDNO ? CloseDecision::discard : CloseDecision::cancelled;
}

bool MainWindow::reset_document_for_open() noexcept {
    document_.close();
    const bool created = document_.create_empty();
    editor_.document_changed();
    update_security_menu();
    update_title();
    if (!created) {
        MessageBoxW(window_, L"无法重新初始化安全文档内存。",
                    L"安全编辑器", MB_OK | MB_ICONERROR);
    }
    return created;
}

bool MainWindow::save(const bool save_as) noexcept {
    if (!document_.open()) return true;
    std::wstring path = document_.path();
    encoding::TextEncoding selected = document_.encoding();
    if (save_as || path.empty()) {
        const io::DialogResult result = io::choose_save_file(
            window_, document_.path(), document_.encoding(), path, selected);
        if (result == io::DialogResult::cancelled) return false;
        if (result == io::DialogResult::failed) {
            MessageBoxW(window_, L"The Save As dialog could not be opened.",
                        L"安全编辑器", MB_OK | MB_ICONERROR);
            return false;
        }
    }
    io::Error error{};
    if (!io::write_document(path, document_, selected, error)) {
        show_io_error(L"Save failed", error);
        return false;
    }
    document_.mark_saved(path, selected);
    update_title();
    // The new path may change whether the file is highlighted (.ini/.env).
    InvalidateRect(window_, nullptr, FALSE);
    return true;
}

void MainWindow::open_dialog() noexcept {
    const CloseDecision decision = confirm_close_document();
    if (decision == CloseDecision::cancelled) return;
    if (decision == CloseDecision::discard && !reset_document_for_open()) return;
    std::wstring path;
    const io::DialogResult result = io::choose_open_file(window_, path);
    if (result == io::DialogResult::selected) load_path(path);
    else if (result == io::DialogResult::failed) {
        MessageBoxW(window_, L"The Open dialog could not be opened.",
                    L"安全编辑器", MB_OK | MB_ICONERROR);
    }
}

void MainWindow::open_path(const std::wstring& path) noexcept {
    const CloseDecision decision = confirm_close_document();
    if (decision == CloseDecision::cancelled) return;
    if (decision == CloseDecision::discard && !reset_document_for_open()) return;
    load_path(path);
}

void MainWindow::load_path(const std::wstring& path) noexcept {
    io::Error error{};
    if (!io::read_document(path, document_, error)) {
        if (!document_.open()) {
            (void)document_.create_empty();
            editor_.document_changed();
            update_security_menu();
            update_title();
        }
        show_io_error(L"Open failed", error);
        return;
    }
    editor_.document_changed();
    update_security_menu();
    update_title();
}

void MainWindow::close_document() noexcept {
    if (confirm_close_document() == CloseDecision::cancelled) return;
    document_.close();
    editor_.document_changed();
    update_security_menu();
    update_title();
}

void MainWindow::fail_closed(const wchar_t*) noexcept {
    if (document_.open()) {
        document_.close();
        editor_.document_changed();
        update_security_menu();
        update_title();
        cleared_for_system_event_ = true;
    }
}

void MainWindow::show_pending_clear_notice() noexcept {
    if (cleared_for_system_event_) {
        cleared_for_system_event_ = false;
        MessageBoxW(window_,
            L"The document was cleared from memory because the session was locked or the system suspended. Unsaved content was not saved.",
            L"安全编辑器", MB_OK | MB_ICONINFORMATION);
    }
}

void MainWindow::show_io_error(const wchar_t* action, const io::Error& error) noexcept {
    wchar_t system_message[192]{};
    if (error.system_code != 0) {
        (void)FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                             nullptr, error.system_code, 0, system_message,
                             static_cast<DWORD>(std::size(system_message)), nullptr);
    }
    wchar_t message[640]{};
    std::swprintf(message, std::size(message),
                  L"%ls.\n\n%ls\nWindows error: %u\nOffset: %zu\n%ls",
                  action, io::error_name(error.code), error.system_code,
                  error.offset, system_message);
    MessageBoxW(window_, message, L"安全编辑器", MB_OK | MB_ICONERROR);
}

void MainWindow::update_title() noexcept {
    if (title_dirty_ != document_.dirty()) {
        title_dirty_ = document_.dirty();
        SetWindowTextW(window_, title_dirty_ ? L"安全编辑器 — 已修改" : L"安全编辑器");
    }
    editor_.status_changed();
}

} // namespace mempad::app
