#pragma once

#include "document/secure_document.h"
#include "editor/line_index.h"
#include "editor/selection.h"
#include "security/secure_allocation.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
#include <windows.h>

namespace mempad::editor {

enum class TabMode {
    literal_tab,
    four_spaces,
};

enum class FontPreset {
    consolas_dengxian,
    consolas_simhei,
    dengxian,
    simhei,
};

class EditorControl final {
public:
    explicit EditorControl(document::SecureDocument& document) noexcept;
    ~EditorControl();

    bool initialize(HWND window) noexcept;
    void shutdown() noexcept;
    void document_changed() noexcept;
    void paint() noexcept;
    void resize(int width, int height) noexcept;
    void key_down(WPARAM key, bool shift, bool control) noexcept;
    void character(char16_t value) noexcept;
    void copy_selection_to_clipboard() noexcept;
    void cut_selection_to_clipboard() noexcept;
    void paste_from_clipboard() noexcept;
    void select_all() noexcept;
    void mouse_down(int x, int y, bool shift) noexcept;
    void mouse_move(int x, int y, bool left_button) noexcept;
    void mouse_up() noexcept;
    void vertical_scroll(WPARAM value) noexcept;
    void horizontal_scroll(WPARAM value) noexcept;
    void mouse_wheel(short delta) noexcept;
    void focus(bool gained) noexcept;
    void status_changed() noexcept;
    void set_tab_mode(TabMode mode) noexcept;
    void set_dark_theme(bool dark) noexcept;
    void set_font(FontPreset preset) noexcept;
    void set_syntax_highlighting(bool enabled) noexcept;
    std::size_t caret() const noexcept { return selection_.caret(); }
    const LineIndex& lines() const noexcept { return lines_; }

private:
    struct WidthCheckpoint final {
        std::uint32_t position = 0;
        int pixels = 0;
    };

    static constexpr std::size_t scratch_bytes = 64U * 1024U;
    static constexpr std::size_t layout_checkpoint_units = 8192U;
    static constexpr int status_height = 24;

    void rebuild() noexcept;
    void rebuild_horizontal_layout() noexcept;
    void apply_font_preset() noexcept;
    void release_editor_fonts() noexcept;
    void update_scrollbars() noexcept;
    void update_caret() noexcept;
    void ensure_caret_visible() noexcept;
    void move_to(std::size_t position, bool extend) noexcept;
    void move_vertical(int line_delta, bool extend) noexcept;
    bool copy_selection_to_clipboard_impl() noexcept;
    void delete_selection() noexcept;
    void insert_text(std::span<const char16_t> text) noexcept;
    std::size_t point_to_position(int x, int y) noexcept;
    int pixel_for_position(HDC dc, std::size_t line,
                           std::size_t position) noexcept;
    std::size_t position_for_pixel(HDC dc, std::size_t line, int pixel,
                                   bool nearest, int& position_pixel) noexcept;
    int measure_text_range(HDC dc, std::size_t begin, std::size_t end) noexcept;
    int draw_text_range(HDC dc, std::size_t begin, std::size_t end,
                        int x, int y, COLORREF foreground,
                        COLORREF background) noexcept;
    void draw_line(HDC dc, std::size_t line, int y) noexcept;
    void notify_change() noexcept;

    document::SecureDocument& document_;
    HWND window_ = nullptr;
    HFONT font_ = nullptr;
    bool owns_font_ = false;
    HFONT cjk_font_ = nullptr;
    bool owns_cjk_font_ = false;
    HFONT ui_font_ = nullptr;
    bool owns_ui_font_ = false;
    int char_width_ = 8;
    int line_height_ = 16;
    int client_width_ = 0;
    int client_height_ = 0;
    std::size_t first_line_ = 0;
    int horizontal_offset_ = 0;
    int horizontal_extent_ = 0;
    std::size_t preferred_column_ = 0;
    LineIndex lines_{};
    std::vector<std::size_t> checkpoint_line_starts_{};
    std::vector<WidthCheckpoint> width_checkpoints_{};
    std::vector<int> line_widths_{};
    Selection selection_{};
    security::SecureAllocation scratch_{};
    security::SecureAllocation input_{};
    bool mouse_selecting_ = false;
    TabMode tab_mode_ = TabMode::literal_tab;
    bool dark_theme_ = false;
    FontPreset font_preset_ = FontPreset::consolas_dengxian;
    bool syntax_highlighting_ = false;
    bool initialized_ = false;
    bool caret_shown_ = false;
};

} // namespace mempad::editor
