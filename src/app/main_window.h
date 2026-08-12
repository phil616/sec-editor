#pragma once

#include "document/secure_document.h"
#include "editor/editor_control.h"
#include "io/io_error.h"

#include <string>
#include <windows.h>

namespace mempad::app {

class MainWindow final {
public:
    explicit MainWindow(HINSTANCE instance) noexcept;
    ~MainWindow();
    bool create(int show_command, const std::wstring& initial_path) noexcept;
    HWND handle() const noexcept { return window_; }

private:
    enum class ThemePreference {
        light,
        dark,
        system,
    };

    enum class CloseDecision {
        cancelled,
        proceed,
        discard,
    };

    static LRESULT CALLBACK window_proc(HWND window, UINT message,
                                       WPARAM wparam, LPARAM lparam) noexcept;
    LRESULT dispatch(UINT message, WPARAM wparam, LPARAM lparam) noexcept;
    bool on_create() noexcept;
    bool create_menu_bar() noexcept;
    void on_command(unsigned command) noexcept;
    void apply_theme() noexcept;
    void update_menu_checks() noexcept;
    void update_security_menu() noexcept;
    void toggle_screen_capture_protection() noexcept;
    void show_about() noexcept;
    CloseDecision confirm_close_document() noexcept;
    bool reset_document_for_open() noexcept;
    bool save(bool save_as) noexcept;
    void open_dialog() noexcept;
    void open_path(const std::wstring& path) noexcept;
    void load_path(const std::wstring& path) noexcept;
    void close_document() noexcept;
    void fail_closed(const wchar_t* reason) noexcept;
    void show_pending_clear_notice() noexcept;
    void show_io_error(const wchar_t* action, const io::Error& error) noexcept;
    void update_title() noexcept;

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    document::SecureDocument document_{};
    editor::EditorControl editor_;
    HMENU menu_bar_ = nullptr;
    HMENU security_menu_ = nullptr;
    HMENU tab_menu_ = nullptr;
    HMENU theme_menu_ = nullptr;
    std::wstring initial_path_{};
    ThemePreference theme_preference_ = ThemePreference::system;
    editor::TabMode tab_mode_ = editor::TabMode::literal_tab;
    bool screen_capture_protection_enabled_ = false;
    bool title_dirty_ = false;
    bool cleared_for_system_event_ = false;
    bool shutting_down_ = false;
};

} // namespace mempad::app
