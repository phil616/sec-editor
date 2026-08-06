#include "editor/editor_control.h"

#include "encoding/encoding.h"
#include "security/process_guard.h"

#include <algorithm>
#include <cwchar>
#include <cstdlib>
#include <limits>
#include <span>

namespace mempad::editor {
static_assert(sizeof(wchar_t) == sizeof(char16_t));
namespace {
constexpr UINT change_message = WM_APP + 1U;

COLORREF editor_background(const bool dark) noexcept {
    return dark ? RGB(32, 32, 32) : GetSysColor(COLOR_WINDOW);
}

COLORREF editor_text(const bool dark) noexcept {
    return dark ? RGB(240, 240, 240) : GetSysColor(COLOR_WINDOWTEXT);
}

COLORREF selection_background(const bool dark) noexcept {
    return dark ? RGB(0, 120, 215) : GetSysColor(COLOR_HIGHLIGHT);
}

COLORREF selection_text(const bool dark) noexcept {
    return dark ? RGB(255, 255, 255) : GetSysColor(COLOR_HIGHLIGHTTEXT);
}

COLORREF status_background(const bool dark) noexcept {
    return dark ? RGB(45, 45, 48) : GetSysColor(COLOR_BTNFACE);
}

COLORREF status_text_color(const bool dark) noexcept {
    return dark ? RGB(225, 225, 225) : GetSysColor(COLOR_BTNTEXT);
}

COLORREF status_border(const bool dark) noexcept {
    return dark ? RGB(80, 80, 80) : GetSysColor(COLOR_3DSHADOW);
}

std::size_t expand_tabs(char16_t* const buffer, const std::size_t count) noexcept {
    std::size_t display_count = count;
    for (std::size_t index = 0; index < count; ++index) {
        if (buffer[index] == u'\t') display_count += 3U;
    }
    std::size_t read = count;
    std::size_t write = display_count;
    while (read != 0) {
        const char16_t value = buffer[--read];
        if (value == u'\t') {
            for (int space = 0; space < 4; ++space) buffer[--write] = u' ';
        } else {
            buffer[--write] = value;
        }
    }
    return display_count;
}
}

EditorControl::EditorControl(document::SecureDocument& document) noexcept
    : document_(document) {}

EditorControl::~EditorControl() { shutdown(); }

bool EditorControl::initialize(HWND window) noexcept {
    window_ = window;
    if (!scratch_.allocate(scratch_bytes) || !input_.allocate(4096)) {
        shutdown();
        return false;
    }
    HDC dc = GetDC(window_);
    if (dc == nullptr) {
        shutdown();
        return false;
    }
    font_ = CreateFontW(-MulDiv(12, GetDeviceCaps(dc, LOGPIXELSY), 72), 0, 0, 0,
                        FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                        FIXED_PITCH | FF_MODERN, L"Consolas");
    owns_font_ = font_ != nullptr;
    if (font_ == nullptr) font_ = static_cast<HFONT>(GetStockObject(SYSTEM_FIXED_FONT));
    const HGDIOBJ previous = SelectObject(dc, font_);
    TEXTMETRICW metrics{};
    if (GetTextMetricsW(dc, &metrics) != 0) {
        char_width_ = std::max(1L, metrics.tmAveCharWidth);
        line_height_ = std::max(1L, metrics.tmHeight + metrics.tmExternalLeading + 4L);
    }
    NONCLIENTMETRICSW nonclient{};
    nonclient.cbSize = sizeof(nonclient);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(nonclient),
                              &nonclient, 0) != 0) {
        ui_font_ = CreateFontIndirectW(&nonclient.lfMessageFont);
    }
    owns_ui_font_ = ui_font_ != nullptr;
    if (ui_font_ == nullptr) ui_font_ = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    SelectObject(dc, previous);
    ReleaseDC(window_, dc);
    CreateCaret(window_, nullptr, 2, line_height_);
    initialized_ = true;
    document_changed();
    return true;
}

void EditorControl::shutdown() noexcept {
    if (caret_shown_ && window_ != nullptr) HideCaret(window_);
    caret_shown_ = false;
    if (window_ != nullptr) DestroyCaret();
    if (owns_font_ && font_ != nullptr) DeleteObject(font_);
    if (owns_ui_font_ && ui_font_ != nullptr) DeleteObject(ui_font_);
    font_ = nullptr;
    owns_font_ = false;
    ui_font_ = nullptr;
    owns_ui_font_ = false;
    scratch_.release();
    input_.release();
    initialized_ = false;
    window_ = nullptr;
}

void EditorControl::document_changed() noexcept {
    if (scratch_.valid()) security::secure_zero(scratch_.data(), scratch_.size());
    if (input_.valid()) security::secure_zero(input_.data(), input_.size());
    selection_.collapse(0);
    first_line_ = 0;
    horizontal_offset_ = 0;
    preferred_column_ = 0;
    rebuild();
    if (window_ != nullptr) {
        InvalidateRect(window_, nullptr, FALSE);
        update_caret();
    }
}

void EditorControl::rebuild() noexcept {
    lines_.rebuild(document_.text());
    rebuild_horizontal_layout();
    update_scrollbars();
}

void EditorControl::rebuild_horizontal_layout() noexcept {
    checkpoint_line_starts_.clear();
    width_checkpoints_.clear();
    line_widths_.clear();
    checkpoint_line_starts_.reserve(lines_.line_count() + 1U);
    line_widths_.reserve(lines_.line_count());

    HDC dc = window_ != nullptr ? GetDC(window_) : nullptr;
    HGDIOBJ previous = nullptr;
    if (dc != nullptr) previous = SelectObject(dc, font_);
    horizontal_extent_ = 0;
    for (std::size_t line = 0; line < lines_.line_count(); ++line) {
        checkpoint_line_starts_.push_back(width_checkpoints_.size());
        const std::size_t start = lines_.line_start(line);
        const std::size_t end = lines_.line_end(line, document_.text());
        std::size_t position = start;
        int pixels = 0;
        width_checkpoints_.push_back(
            {static_cast<std::uint32_t>(position), pixels});
        while (position < end) {
            std::size_t next = std::min(end, position + layout_checkpoint_units);
            if (next < end && next > position &&
                document::is_low_surrogate(document_.text().at(next)) &&
                document::is_high_surrogate(document_.text().at(next - 1U))) {
                --next;
            }
            if (next == position) next = document_.text().next_scalar(position);
            const int advance = dc != nullptr
                ? measure_text_range(dc, position, next)
                : static_cast<int>(next - position) * char_width_;
            const long long total = static_cast<long long>(pixels) + advance;
            pixels = static_cast<int>(std::min<long long>(total, INT_MAX));
            position = next;
            width_checkpoints_.push_back(
                {static_cast<std::uint32_t>(position), pixels});
        }
        line_widths_.push_back(pixels);
        horizontal_extent_ = std::max(horizontal_extent_, pixels);
    }
    checkpoint_line_starts_.push_back(width_checkpoints_.size());
    if (dc != nullptr) {
        SelectObject(dc, previous);
        ReleaseDC(window_, dc);
    }
}

void EditorControl::update_scrollbars() noexcept {
    if (window_ == nullptr) return;
    SCROLLINFO vertical{};
    vertical.cbSize = sizeof(SCROLLINFO);
    vertical.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    vertical.nMin = 0;
    vertical.nMax = static_cast<int>(std::min<std::size_t>(lines_.line_count() - 1U,
                                                           INT_MAX));
    vertical.nPage = static_cast<UINT>(
        std::max(1, (client_height_ - status_height) / line_height_));
    vertical.nPos = static_cast<int>(std::min<std::size_t>(first_line_, INT_MAX));
    SetScrollInfo(window_, SB_VERT, &vertical, TRUE);

    SCROLLINFO horizontal{};
    horizontal.cbSize = sizeof(SCROLLINFO);
    horizontal.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    horizontal.nMin = 0;
    horizontal.nMax = std::max(0, horizontal_extent_ - 1);
    horizontal.nPage = static_cast<UINT>(std::max(1, client_width_ - 8));
    horizontal.nPos = horizontal_offset_;
    SetScrollInfo(window_, SB_HORZ, &horizontal, TRUE);
    horizontal.fMask = SIF_POS;
    if (GetScrollInfo(window_, SB_HORZ, &horizontal) != FALSE) {
        horizontal_offset_ = horizontal.nPos;
    }
}

void EditorControl::resize(const int width, const int height) noexcept {
    client_width_ = width;
    client_height_ = height;
    update_scrollbars();
    update_caret();
}

int EditorControl::pixel_for_position(HDC dc, const std::size_t line,
                                      const std::size_t position) noexcept {
    if (line >= lines_.line_count() || line + 1U >= checkpoint_line_starts_.size()) {
        return 0;
    }
    const std::size_t bounded = std::clamp(
        position, lines_.line_start(line), lines_.line_end(line, document_.text()));
    const auto first = width_checkpoints_.begin() +
        static_cast<std::ptrdiff_t>(checkpoint_line_starts_[line]);
    const auto last = width_checkpoints_.begin() +
        static_cast<std::ptrdiff_t>(checkpoint_line_starts_[line + 1U]);
    auto checkpoint = std::upper_bound(
        first, last, bounded,
        [](const std::size_t value, const WidthCheckpoint& candidate) {
            return value < candidate.position;
        });
    if (checkpoint != first) --checkpoint;
    const std::size_t checkpoint_position = checkpoint->position;
    const int remainder = measure_text_range(dc, checkpoint_position, bounded);
    return static_cast<int>(std::min<long long>(
        static_cast<long long>(checkpoint->pixels) + remainder, INT_MAX));
}

std::size_t EditorControl::position_for_pixel(HDC dc, const std::size_t line,
                                              const int pixel, const bool nearest,
                                              int& position_pixel) noexcept {
    const std::size_t start = lines_.line_start(line);
    const std::size_t end = lines_.line_end(line, document_.text());
    if (line >= line_widths_.size() || line + 1U >= checkpoint_line_starts_.size()) {
        position_pixel = 0;
        return start;
    }
    const int target = std::max(0, pixel);
    if (target >= line_widths_[line]) {
        position_pixel = line_widths_[line];
        return end;
    }
    const auto first = width_checkpoints_.begin() +
        static_cast<std::ptrdiff_t>(checkpoint_line_starts_[line]);
    const auto last = width_checkpoints_.begin() +
        static_cast<std::ptrdiff_t>(checkpoint_line_starts_[line + 1U]);
    auto checkpoint = std::upper_bound(
        first, last, target,
        [](const int value, const WidthCheckpoint& candidate) {
            return value < candidate.pixels;
        });
    if (checkpoint != first) --checkpoint;
    const std::size_t checkpoint_position = checkpoint->position;
    const auto following = checkpoint + 1;
    std::size_t low = checkpoint_position;
    std::size_t high = following != last
        ? static_cast<std::size_t>(following->position) : end;
    int low_pixels = checkpoint->pixels;
    while (high > low + 1U) {
        std::size_t middle = low + (high - low) / 2U;
        if (middle > low && middle < end &&
            document::is_low_surrogate(document_.text().at(middle)) &&
            document::is_high_surrogate(document_.text().at(middle - 1U))) {
            --middle;
        }
        if (middle <= low) middle = document_.text().next_scalar(low);
        if (middle >= high) break;
        const int middle_pixels = static_cast<int>(std::min<long long>(
            static_cast<long long>(checkpoint->pixels) +
                measure_text_range(dc, checkpoint_position, middle),
            INT_MAX));
        if (middle_pixels <= target) {
            low = middle;
            low_pixels = middle_pixels;
        } else {
            high = middle;
        }
    }
    if (low < end) {
        const std::size_t next = document_.text().next_scalar(low);
        const int advance = measure_text_range(dc, low, next);
        if (nearest && target >= low_pixels + (advance + 1) / 2) {
            low = next;
            low_pixels += advance;
        }
    }
    position_pixel = low_pixels;
    return low;
}

int EditorControl::measure_text_range(HDC dc, std::size_t begin,
                                      const std::size_t end) noexcept {
    if (end <= begin || !scratch_.valid()) return 0;
    auto* buffer = reinterpret_cast<char16_t*>(scratch_.data());
    const std::size_t capacity = scratch_.size() / sizeof(char16_t);
    const std::size_t input_capacity = std::max<std::size_t>(1U, capacity / 4U);
    long long total = 0;
    while (begin < end) {
        const std::size_t count = std::min(input_capacity, end - begin);
        if (!document_.text().copy_range(begin, begin + count,
                                         std::span<char16_t>(buffer, count))) break;
        const std::size_t display_count = expand_tabs(buffer, count);
        SIZE extent{};
        if (GetTextExtentPoint32W(dc, reinterpret_cast<const wchar_t*>(buffer),
                                  static_cast<int>(display_count), &extent) != 0) {
            total += extent.cx;
        }
        security::secure_zero(buffer, display_count * sizeof(char16_t));
        begin += count;
    }
    return static_cast<int>(std::min<long long>(total, INT_MAX));
}

int EditorControl::draw_text_range(HDC dc, std::size_t begin, const std::size_t end,
                                   int x, const int y, const COLORREF foreground,
                                   const COLORREF background) noexcept {
    if (end <= begin || !scratch_.valid()) return 0;
    auto* buffer = reinterpret_cast<char16_t*>(scratch_.data());
    const std::size_t capacity = scratch_.size() / sizeof(char16_t);
    const std::size_t input_capacity = std::max<std::size_t>(1U, capacity / 4U);
    int total = 0;
    SetTextColor(dc, foreground);
    SetBkColor(dc, background);
    while (begin < end) {
        const std::size_t count = std::min(input_capacity, end - begin);
        if (!document_.text().copy_range(begin, begin + count,
                                         std::span<char16_t>(buffer, count))) break;
        const std::size_t display_count = expand_tabs(buffer, count);
        SIZE extent{};
        if (GetTextExtentPoint32W(dc, reinterpret_cast<const wchar_t*>(buffer),
                                  static_cast<int>(display_count), &extent) == 0) {
            extent.cx = static_cast<LONG>(display_count) * char_width_;
        }
        const RECT area{x, y, x + extent.cx, y + line_height_};
        ExtTextOutW(dc, x, y, ETO_OPAQUE, &area,
                    reinterpret_cast<const wchar_t*>(buffer),
                    static_cast<UINT>(display_count), nullptr);
        security::secure_zero(buffer, display_count * sizeof(char16_t));
        begin += count;
        x += extent.cx;
        total += extent.cx;
    }
    return total;
}

void EditorControl::draw_line(HDC dc, const std::size_t line, const int y) noexcept {
    const std::size_t end = lines_.line_end(line, document_.text());
    int start_pixel = 0;
    const std::size_t visible_start = position_for_pixel(
        dc, line, horizontal_offset_, false, start_pixel);
    int end_pixel = 0;
    std::size_t visible_end = position_for_pixel(
        dc, line, horizontal_offset_ + std::max(1, client_width_ - 8),
        false, end_pixel);
    if (visible_end < end) visible_end = document_.text().next_scalar(visible_end);
    const std::size_t select_begin = std::clamp(selection_.begin(), visible_start, visible_end);
    const std::size_t select_end = std::clamp(selection_.end(), visible_start, visible_end);
    HBRUSH background = CreateSolidBrush(editor_background(dark_theme_));
    const int content_left = std::min(4, std::max(0, client_width_));
    const RECT margin{0, y, content_left, y + line_height_};
    FillRect(dc, &margin, background);
    const int saved = SaveDC(dc);
    IntersectClipRect(dc, content_left, y, client_width_, y + line_height_);
    int x = 4 + start_pixel - horizontal_offset_;
    x += draw_text_range(dc, visible_start, select_begin, x, y,
                         editor_text(dark_theme_), editor_background(dark_theme_));
    x += draw_text_range(dc, select_begin, select_end, x, y,
                         selection_text(dark_theme_), selection_background(dark_theme_));
    x += draw_text_range(dc, select_end, visible_end, x, y,
                         editor_text(dark_theme_), editor_background(dark_theme_));
    RestoreDC(dc, saved);
    const RECT tail{std::clamp(x, content_left, client_width_), y,
                    client_width_, y + line_height_};
    FillRect(dc, &tail, background);
    DeleteObject(background);
}

void EditorControl::paint() noexcept {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(window_, &paint);
    if (dc == nullptr) return;
    RECT client{};
    GetClientRect(window_, &client);
    const HGDIOBJ previous = SelectObject(dc, font_);
    SetBkMode(dc, OPAQUE);
    const int editor_bottom = static_cast<int>(std::max<LONG>(0, client.bottom - status_height));
    const std::size_t visible_lines = static_cast<std::size_t>(
        std::max(1, (editor_bottom + line_height_ - 1) / line_height_));
    HBRUSH background = CreateSolidBrush(editor_background(dark_theme_));
    for (std::size_t row = 0; row < visible_lines; ++row) {
        const int y = static_cast<int>(row) * line_height_;
        const int bottom = std::min(editor_bottom, y + line_height_);
        if (bottom <= paint.rcPaint.top || y >= paint.rcPaint.bottom) continue;
        if (first_line_ + row < lines_.line_count()) {
            draw_line(dc, first_line_ + row, y);
        } else {
            const RECT empty{0, y, client.right, bottom};
            FillRect(dc, &empty, background);
        }
    }
    DeleteObject(background);

    RECT status{0, editor_bottom, client.right, client.bottom};
    HBRUSH status_brush = CreateSolidBrush(status_background(dark_theme_));
    FillRect(dc, &status, status_brush);
    DeleteObject(status_brush);
    SelectObject(dc, ui_font_);
    SetBkMode(dc, TRANSPARENT);
    HPEN status_separator = CreatePen(PS_SOLID, 1, status_border(dark_theme_));
    const HGDIOBJ previous_pen = SelectObject(dc, status_separator);
    MoveToEx(dc, 0, editor_bottom, nullptr);
    LineTo(dc, client.right, editor_bottom);
    SelectObject(dc, previous_pen);
    DeleteObject(status_separator);
    const std::size_t line = lines_.line_for_position(selection_.caret());
    const std::size_t column = selection_.caret() - lines_.line_start(line);
    wchar_t status_text[320]{};
    const wchar_t* const mixed = document_.newline().mixed ? L"混合换行 → " : L"";
    std::swprintf(status_text, std::size(status_text),
                  L"  行 %zu，列 %zu    %ls    %ls%ls    %ls    安全内存：%ls",
                  line + 1U, column + 1U, encoding::encoding_name(document_.encoding()),
                  mixed, encoding::newline_name(document_.newline().selected),
                  document_.dirty() ? L"已修改" : L"已保存",
                  document_.secure() ? L"是" : L"无文档");
    SetTextColor(dc, status_text_color(dark_theme_));
    ExtTextOutW(dc, 8, editor_bottom + 5, ETO_CLIPPED, &status, status_text,
                static_cast<UINT>(std::wcslen(status_text)), nullptr);
    SelectObject(dc, previous);
    EndPaint(window_, &paint);
}

void EditorControl::update_caret() noexcept {
    if (!initialized_ || window_ == nullptr || !document_.open()) {
        focus(false);
        return;
    }
    const std::size_t line = lines_.line_for_position(selection_.caret());
    int x = 4;
    HDC dc = GetDC(window_);
    if (dc != nullptr) {
        const HGDIOBJ previous = SelectObject(dc, font_);
        x += pixel_for_position(dc, line, selection_.caret()) - horizontal_offset_;
        SelectObject(dc, previous);
        ReleaseDC(window_, dc);
    }
    const int y = static_cast<int>(line - std::min(line, first_line_)) * line_height_;
    SetCaretPos(x, y);
}

void EditorControl::focus(const bool gained) noexcept {
    if (!initialized_ || window_ == nullptr) return;
    if (gained && document_.open() && !caret_shown_) {
        caret_shown_ = ShowCaret(window_) != 0;
        update_caret();
    } else if ((!gained || !document_.open()) && caret_shown_) {
        HideCaret(window_);
        caret_shown_ = false;
    }
    if (!gained && input_.valid()) {
        security::secure_zero(input_.data(), input_.size());
    }
}

void EditorControl::ensure_caret_visible() noexcept {
    const std::size_t line = lines_.line_for_position(selection_.caret());
    const std::size_t page_lines = static_cast<std::size_t>(
        std::max(1, (client_height_ - status_height) / line_height_));
    if (line < first_line_) first_line_ = line;
    else if (line >= first_line_ + page_lines) first_line_ = line - page_lines + 1U;

    HDC dc = GetDC(window_);
    if (dc != nullptr) {
        const HGDIOBJ previous = SelectObject(dc, font_);
        const int caret_pixel = pixel_for_position(dc, line, selection_.caret());
        const int available = std::max(1, client_width_ - 8);
        if (caret_pixel < horizontal_offset_) horizontal_offset_ = caret_pixel;
        else if (caret_pixel > horizontal_offset_ + available) {
            horizontal_offset_ = caret_pixel - available;
        }
        SelectObject(dc, previous);
        ReleaseDC(window_, dc);
    }
    update_scrollbars();
    update_caret();
    InvalidateRect(window_, nullptr, FALSE);
}

void EditorControl::move_to(const std::size_t position, const bool extend) noexcept {
    const std::size_t bounded = std::min(position, document_.text().size());
    if (extend) selection_.extend(bounded);
    else selection_.collapse(bounded);
    const std::size_t line = lines_.line_for_position(bounded);
    preferred_column_ = bounded - lines_.line_start(line);
    ensure_caret_visible();
}

void EditorControl::move_vertical(const int line_delta, const bool extend) noexcept {
    const std::size_t current_line = lines_.line_for_position(selection_.caret());
    const long long candidate = static_cast<long long>(current_line) + line_delta;
    const std::size_t target_line = candidate < 0 ? 0U :
        std::min<std::size_t>(static_cast<std::size_t>(candidate), lines_.line_count() - 1U);
    std::size_t target = lines_.position(target_line, preferred_column_, document_.text());
    if (target < document_.text().size() && document::is_low_surrogate(document_.text().at(target)) &&
        target != 0 && document::is_high_surrogate(document_.text().at(target - 1U))) {
        --target;
    }
    if (extend) selection_.extend(target); else selection_.collapse(target);
    ensure_caret_visible();
}

void EditorControl::delete_selection() noexcept {
    if (!selection_.empty() && document_.erase(selection_.begin(), selection_.end())) {
        selection_.collapse(selection_.begin());
        rebuild();
        notify_change();
    }
}

void EditorControl::insert_text(const std::span<const char16_t> text) noexcept {
    const std::size_t begin = selection_.begin();
    const std::size_t end = selection_.end();
    if (document_.replace(begin, end, text)) {
        selection_.collapse(begin + text.size());
        rebuild();
        notify_change();
    } else {
        MessageBoxW(window_, L"The edit exceeds a document or line safety limit, or secure memory could not be extended.",
                    L"安全编辑器", MB_OK | MB_ICONERROR);
    }
}

void EditorControl::key_down(const WPARAM key, const bool shift,
                             const bool control) noexcept {
    if (!document_.open()) return;
    if (control && key == 'A') {
        selection_.set(0, document_.text().size());
        ensure_caret_visible();
        return;
    }
    std::size_t target = selection_.caret();
    switch (key) {
    case VK_LEFT:
        if (!shift && !selection_.empty()) target = selection_.begin();
        else target = document_.text().previous_scalar(target);
        move_to(target, shift);
        break;
    case VK_RIGHT:
        if (!shift && !selection_.empty()) target = selection_.end();
        else target = document_.text().next_scalar(target);
        move_to(target, shift);
        break;
    case VK_UP: move_vertical(-1, shift); break;
    case VK_DOWN: move_vertical(1, shift); break;
    case VK_HOME:
        move_to(lines_.line_start(lines_.line_for_position(target)), shift); break;
    case VK_END:
        move_to(lines_.line_end(lines_.line_for_position(target), document_.text()), shift); break;
    case VK_PRIOR:
        move_vertical(-std::max(1, (client_height_ - status_height) /
                                   line_height_), shift); break;
    case VK_NEXT:
        move_vertical(std::max(1, (client_height_ - status_height) /
                                  line_height_), shift); break;
    case VK_BACK:
        if (!selection_.empty()) delete_selection();
        else if (target != 0) {
            const std::size_t previous = document_.text().previous_scalar(target);
            if (document_.erase(previous, target)) {
                selection_.collapse(previous); rebuild(); notify_change();
            }
        }
        break;
    case VK_DELETE:
        if (!selection_.empty()) delete_selection();
        else if (target < document_.text().size()) {
            const std::size_t next = document_.text().next_scalar(target);
            if (document_.erase(target, next)) { rebuild(); notify_change(); }
        }
        break;
    default: break;
    }
}

void EditorControl::character(const char16_t value) noexcept {
    if (!document_.open()) return;
    auto* pending = reinterpret_cast<char16_t*>(input_.data());
    if (document::is_high_surrogate(value)) {
        pending[0] = value;
        return;
    }
    if (document::is_low_surrogate(value)) {
        if (document::is_high_surrogate(pending[0])) {
            pending[1] = value;
            insert_text(std::span<const char16_t>(pending, 2));
            security::secure_zero(pending, 2U * sizeof(char16_t));
        }
        return;
    }
    security::secure_zero(pending, 2U * sizeof(char16_t));
    if (value == u'\r') {
        const char16_t newline = u'\n';
        insert_text(std::span<const char16_t>(&newline, 1));
    } else if (value == u'\t' && tab_mode_ == TabMode::four_spaces) {
        constexpr char16_t spaces[]{u' ', u' ', u' ', u' '};
        insert_text(std::span<const char16_t>(spaces));
    } else if (value == u'\t' || value >= u' ') {
        insert_text(std::span<const char16_t>(&value, 1));
    }
}

std::size_t EditorControl::point_to_position(const int x, const int y) noexcept {
    const std::size_t line = std::min(first_line_ + static_cast<std::size_t>(
        std::max(0, y) / line_height_), lines_.line_count() - 1U);
    HDC dc = GetDC(window_);
    if (dc == nullptr) return lines_.line_start(line);
    const HGDIOBJ previous = SelectObject(dc, font_);
    int position_pixel = 0;
    const int target = horizontal_offset_ + std::max(0, x - 4);
    const std::size_t position = position_for_pixel(
        dc, line, target, true, position_pixel);
    SelectObject(dc, previous);
    ReleaseDC(window_, dc);
    return position;
}

void EditorControl::mouse_down(const int x, const int y, const bool shift) noexcept {
    if (!document_.open() || y < 0 || y >= client_height_ - status_height) return;
    SetFocus(window_);
    const std::size_t position = point_to_position(x, y);
    if (shift) selection_.extend(position); else selection_.collapse(position);
    mouse_selecting_ = true;
    SetCapture(window_);
    ensure_caret_visible();
}

void EditorControl::mouse_move(const int x, const int y, const bool left_button) noexcept {
    if (mouse_selecting_ && left_button) {
        selection_.extend(point_to_position(x, y));
        ensure_caret_visible();
    }
}

void EditorControl::mouse_up() noexcept {
    if (mouse_selecting_) ReleaseCapture();
    mouse_selecting_ = false;
}

void EditorControl::vertical_scroll(const WPARAM value) noexcept {
    SCROLLINFO info{};
    info.cbSize = sizeof(SCROLLINFO);
    info.fMask = SIF_ALL;
    GetScrollInfo(window_, SB_VERT, &info);
    std::size_t next = first_line_;
    switch (LOWORD(value)) {
    case SB_LINEUP: if (next != 0) --next; break;
    case SB_LINEDOWN: ++next; break;
    case SB_PAGEUP: next = next > info.nPage ? next - info.nPage : 0; break;
    case SB_PAGEDOWN: next += info.nPage; break;
    case SB_THUMBTRACK: next = static_cast<std::size_t>(info.nTrackPos); break;
    default: return;
    }
    first_line_ = std::min(next, lines_.line_count() - 1U);
    update_scrollbars(); update_caret(); InvalidateRect(window_, nullptr, FALSE);
}

void EditorControl::horizontal_scroll(const WPARAM value) noexcept {
    SCROLLINFO info{};
    info.cbSize = sizeof(SCROLLINFO);
    info.fMask = SIF_ALL;
    GetScrollInfo(window_, SB_HORZ, &info);
    int next = horizontal_offset_;
    const int line_step = std::max(1, char_width_);
    const int page_step = std::max(line_step, static_cast<int>(info.nPage) - line_step);
    switch (LOWORD(value)) {
    case SB_LINELEFT: next -= line_step; break;
    case SB_LINERIGHT: next += line_step; break;
    case SB_PAGELEFT: next -= page_step; break;
    case SB_PAGERIGHT: next += page_step; break;
    case SB_THUMBTRACK: next = info.nTrackPos; break;
    case SB_LEFT: next = 0; break;
    case SB_RIGHT: next = info.nMax; break;
    default: return;
    }
    const int maximum = std::max(
        0, info.nMax - static_cast<int>(info.nPage) + 1);
    horizontal_offset_ = std::clamp(next, 0, maximum);
    update_scrollbars(); update_caret(); InvalidateRect(window_, nullptr, FALSE);
}

void EditorControl::mouse_wheel(const short delta) noexcept {
    const int lines = std::max(1, std::abs(static_cast<int>(delta)) / WHEEL_DELTA) * 3;
    for (int i = 0; i < lines; ++i) {
        vertical_scroll(MAKEWPARAM(delta > 0 ? SB_LINEUP : SB_LINEDOWN, 0));
    }
}

void EditorControl::set_tab_mode(const TabMode mode) noexcept {
    tab_mode_ = mode;
}

void EditorControl::set_dark_theme(const bool dark) noexcept {
    if (dark_theme_ == dark) return;
    dark_theme_ = dark;
    if (window_ != nullptr) InvalidateRect(window_, nullptr, FALSE);
}

void EditorControl::status_changed() noexcept {
    if (window_ == nullptr) return;
    RECT client{};
    GetClientRect(window_, &client);
    const RECT status{0, std::max<LONG>(0, client.bottom - status_height),
                      client.right, client.bottom};
    InvalidateRect(window_, &status, FALSE);
}

void EditorControl::notify_change() noexcept {
    const std::size_t line = lines_.line_for_position(selection_.caret());
    preferred_column_ = selection_.caret() - lines_.line_start(line);
    ensure_caret_visible();
    PostMessageW(window_, change_message, 0, 0);
}

} // namespace mempad::editor
