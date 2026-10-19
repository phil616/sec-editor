#include "editor/editor_control.h"

#include "editor/clipboard_text.h"
#include "editor/ini_env_highlight.h"
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

class ClipboardSession final {
public:
    explicit ClipboardSession(HWND owner) noexcept
        : open_(OpenClipboard(owner) != FALSE) {}
    ~ClipboardSession() { if (open_) CloseClipboard(); }
    bool open() const noexcept { return open_; }
private:
    bool open_ = false;
};

class GlobalMemoryLock final {
public:
    explicit GlobalMemoryLock(HGLOBAL memory) noexcept
        : memory_(memory), pointer_(GlobalLock(memory)) {}
    ~GlobalMemoryLock() { if (pointer_ != nullptr) GlobalUnlock(memory_); }
    const wchar_t* text() const noexcept {
        return static_cast<const wchar_t*>(pointer_);
    }
private:
    HGLOBAL memory_ = nullptr;
    void* pointer_ = nullptr;
};

bool read_clipboard_text(HWND owner, security::SecureAllocation& destination,
                         std::size_t& units, const wchar_t*& error) noexcept {
    units = 0;
    error = nullptr;
    if (IsClipboardFormatAvailable(CF_UNICODETEXT) == FALSE) {
        error = L"剪贴板中没有可粘贴的 Unicode 文本。";
        return false;
    }
    ClipboardSession clipboard(owner);
    if (!clipboard.open()) {
        error = L"无法打开剪贴板。";
        return false;
    }
    HGLOBAL memory = reinterpret_cast<HGLOBAL>(GetClipboardData(CF_UNICODETEXT));
    if (memory == nullptr) {
        error = L"无法读取剪贴板文本。";
        return false;
    }
    const SIZE_T byte_count = GlobalSize(memory);
    if (byte_count < sizeof(wchar_t)) {
        error = L"剪贴板文本格式无效。";
        return false;
    }
    GlobalMemoryLock locked(memory);
    const wchar_t* source = locked.text();
    if (source == nullptr) {
        error = L"无法读取剪贴板文本。";
        return false;
    }
    const std::size_t capacity = static_cast<std::size_t>(byte_count) /
                                 sizeof(wchar_t);
    constexpr std::size_t max_source_units =
        document::SecureGapBuffer::max_units * 2U;
    const std::size_t scan_limit = std::min(capacity, max_source_units + 1U);
    std::size_t source_units = 0;
    while (source_units < scan_limit && source[source_units] != L'\0') {
        ++source_units;
    }
    if (source_units == scan_limit) {
        error = L"剪贴板文本无效或超过文档安全限制。";
        return false;
    }
    const std::span<const char16_t> input(
        reinterpret_cast<const char16_t*>(source), source_units);
    if (!clipboard_text_normalized_size(input, units) ||
        units > document::SecureGapBuffer::max_units) {
        error = L"剪贴板文本包含无效 Unicode 或超过文档安全限制。";
        return false;
    }
    if (units == 0) return true;
    const std::size_t bytes = units * sizeof(char16_t);
    if (!destination.allocate(bytes)) {
        error = L"无法为剪贴板文本分配安全内存。";
        return false;
    }
    const std::span<char16_t> output(
        reinterpret_cast<char16_t*>(destination.data()), units);
    if (!normalize_clipboard_text(input, output)) {
        error = L"无法规范化剪贴板文本。";
        return false;
    }
    return true;
}

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

bool code_point_uses_cjk_font(const char16_t value) noexcept {
    return (value >= 0x1100U && value <= 0x11FFU) ||  // Hangul Jamo
           (value >= 0x2E80U && value <= 0x9FFFU) ||  // CJK radicals, symbols,
                                                      // punctuation, ideographs
           (value >= 0xAC00U && value <= 0xD7AFU) ||  // Hangul syllables
           (value >= 0xF900U && value <= 0xFAFFU) ||  // CJK compatibility ideographs
           (value >= 0xFE30U && value <= 0xFE4FU) ||  // CJK compatibility forms
           (value >= 0xFF00U && value <= 0xFFEFU);    // fullwidth forms
}

bool scalar_uses_cjk_font(const document::SecureGapBuffer& text,
                          const std::size_t position) noexcept {
    const char16_t first = text.at(position);
    if (document::is_high_surrogate(first)) {
        const std::size_t next = position + 1U;
        if (next < text.size() && document::is_low_surrogate(text.at(next))) {
            const std::uint32_t code_point = 0x10000U +
                ((static_cast<std::uint32_t>(first) - 0xD800U) << 10) +
                (static_cast<std::uint32_t>(text.at(next)) - 0xDC00U);
            return code_point >= 0x20000U; // CJK Extension B and later
        }
        return false;
    }
    if (document::is_low_surrogate(first)) return false; // split scalar
    return code_point_uses_cjk_font(first);
}

// Splits [begin, end) into runs of scalars sharing the same font, selects the
// font for each run on `dc`, and invokes the callback with the run bounds.
template <typename Callback>
void for_each_font_run(const document::SecureGapBuffer& text, HDC dc,
                       HFONT latin, HFONT cjk, std::size_t begin,
                       const std::size_t end, Callback&& callback) noexcept {
    const std::size_t bounded_end = std::min(end, text.size());
    std::size_t position = std::min(begin, bounded_end);
    while (position < bounded_end) {
        const bool cjk_run = scalar_uses_cjk_font(text, position);
        SelectObject(dc, cjk_run ? cjk : latin);
        std::size_t run_end = text.next_scalar(position);
        while (run_end < bounded_end && scalar_uses_cjk_font(text, run_end) == cjk_run) {
            run_end = text.next_scalar(run_end);
        }
        callback(position, run_end);
        position = run_end;
    }
}

constexpr std::size_t max_highlight_tokens = 8;

COLORREF highlight_color(const highlight::TokenKind kind,
                         const bool dark) noexcept {
    switch (kind) {
    case highlight::TokenKind::comment:
        return dark ? RGB(106, 153, 85) : RGB(0, 128, 0);
    case highlight::TokenKind::section:
        return dark ? RGB(78, 201, 176) : RGB(0, 128, 128);
    case highlight::TokenKind::key:
        return dark ? RGB(86, 156, 214) : RGB(0, 0, 200);
    case highlight::TokenKind::operator_:
        return dark ? RGB(128, 128, 128) : RGB(120, 120, 120);
    case highlight::TokenKind::string_:
        return dark ? RGB(206, 145, 120) : RGB(163, 21, 21);
    case highlight::TokenKind::number:
        return dark ? RGB(197, 134, 192) : RGB(128, 0, 128);
    case highlight::TokenKind::plain:
        break;
    }
    return editor_text(dark);
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
    font_preset_ = FontPreset::consolas_dengxian;
    apply_font_preset();
    if (font_ == nullptr) {
        shutdown();
        return false;
    }
    NONCLIENTMETRICSW nonclient{};
    nonclient.cbSize = sizeof(nonclient);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(nonclient),
                              &nonclient, 0) != 0) {
        ui_font_ = CreateFontIndirectW(&nonclient.lfMessageFont);
    }
    owns_ui_font_ = ui_font_ != nullptr;
    if (ui_font_ == nullptr) ui_font_ = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    initialized_ = true;
    document_changed();
    return true;
}

void EditorControl::shutdown() noexcept {
    find_query_.clear();
    if (caret_shown_ && window_ != nullptr) HideCaret(window_);
    caret_shown_ = false;
    if (window_ != nullptr) DestroyCaret();
    release_editor_fonts();
    if (owns_ui_font_ && ui_font_ != nullptr) DeleteObject(ui_font_);
    ui_font_ = nullptr;
    owns_ui_font_ = false;
    scratch_.release();
    input_.release();
    initialized_ = false;
    window_ = nullptr;
}

void EditorControl::release_editor_fonts() noexcept {
    if (owns_font_ && font_ != nullptr) DeleteObject(font_);
    if (owns_cjk_font_ && cjk_font_ != nullptr && cjk_font_ != font_) {
        DeleteObject(cjk_font_);
    }
    font_ = nullptr;
    cjk_font_ = nullptr;
    owns_font_ = false;
    owns_cjk_font_ = false;
}

void EditorControl::apply_font_preset() noexcept {
    if (window_ == nullptr) return;
    HDC dc = GetDC(window_);
    if (dc == nullptr) return;
    release_editor_fonts();

    const wchar_t* latin = L"Consolas";
    const wchar_t* cjk = L"DengXian";
    switch (font_preset_) {
    case FontPreset::consolas_dengxian:
        latin = L"Consolas"; cjk = L"DengXian"; break;
    case FontPreset::consolas_simhei:
        latin = L"Consolas"; cjk = L"SimHei"; break;
    case FontPreset::dengxian:
        latin = L"DengXian"; cjk = L"DengXian"; break;
    case FontPreset::simhei:
        latin = L"SimHei"; cjk = L"SimHei"; break;
    }
    const auto create = [dc](const wchar_t* face, const bool fixed) noexcept {
        return CreateFontW(-MulDiv(12, GetDeviceCaps(dc, LOGPIXELSY), 72), 0, 0, 0,
                           FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY,
                           fixed ? (FIXED_PITCH | FF_MODERN)
                                 : (DEFAULT_PITCH | FF_DONTCARE),
                           face);
    };
    font_ = create(latin, true);
    owns_font_ = font_ != nullptr;
    if (font_ == nullptr) font_ = static_cast<HFONT>(GetStockObject(SYSTEM_FIXED_FONT));
    if (std::wcscmp(latin, cjk) == 0) {
        cjk_font_ = font_;
        owns_cjk_font_ = false;
    } else {
        cjk_font_ = create(cjk, false);
        owns_cjk_font_ = cjk_font_ != nullptr;
        if (cjk_font_ == nullptr) cjk_font_ = font_;
    }

    // Line height covers the taller of the two fonts so CJK glyphs never clip.
    char_width_ = 8;
    int metrics_height = 0;
    const HGDIOBJ previous = SelectObject(dc, font_);
    TEXTMETRICW latin_metrics{};
    if (GetTextMetricsW(dc, &latin_metrics) != 0) {
        char_width_ = std::max(1L, latin_metrics.tmAveCharWidth);
        metrics_height = std::max(metrics_height,
            static_cast<int>(latin_metrics.tmHeight + latin_metrics.tmExternalLeading));
    }
    SelectObject(dc, cjk_font_);
    TEXTMETRICW cjk_metrics{};
    if (GetTextMetricsW(dc, &cjk_metrics) != 0) {
        metrics_height = std::max(metrics_height,
            static_cast<int>(cjk_metrics.tmHeight + cjk_metrics.tmExternalLeading));
    }
    SelectObject(dc, previous);
    ReleaseDC(window_, dc);
    line_height_ = std::max(1, metrics_height + 4);

    if (caret_shown_) HideCaret(window_);
    DestroyCaret();
    CreateCaret(window_, nullptr, 2, line_height_);
    if (caret_shown_) (void)ShowCaret(window_);

    rebuild_horizontal_layout();
    update_scrollbars();
    update_caret();
    InvalidateRect(window_, nullptr, FALSE);
}

void EditorControl::document_changed() noexcept {
    // The document is already replaced/cleared; discard find state without
    // measuring the old selection against the new text and stale line index.
    close_find(false);
    mouse_up();
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
        std::max(1, editor_bottom() / line_height_));
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
    return measure_buffer_range(dc, document_.text(), begin, end);
}

int EditorControl::measure_buffer_range(HDC dc,
    const document::SecureGapBuffer& text, std::size_t begin,
    const std::size_t end) noexcept {
    if (end <= begin || !scratch_.valid()) return 0;
    auto* buffer = reinterpret_cast<char16_t*>(scratch_.data());
    const std::size_t capacity = scratch_.size() / sizeof(char16_t);
    const std::size_t input_capacity = std::max<std::size_t>(1U, capacity / 4U);
    long long total = 0;
    for_each_font_run(text, dc, font_, cjk_font_, begin, end,
        [&](const std::size_t run_begin, const std::size_t run_end) {
            std::size_t position = run_begin;
            while (position < run_end) {
                const std::size_t count = std::min(input_capacity, run_end - position);
                if (!text.copy_range(position, position + count,
                                                 std::span<char16_t>(buffer, count))) break;
                const std::size_t display_count = expand_tabs(buffer, count);
                SIZE extent{};
                if (GetTextExtentPoint32W(dc, reinterpret_cast<const wchar_t*>(buffer),
                                          static_cast<int>(display_count), &extent) != 0) {
                    total += extent.cx;
                }
                security::secure_zero(buffer, display_count * sizeof(char16_t));
                position += count;
            }
        });
    return static_cast<int>(std::min<long long>(total, INT_MAX));
}

int EditorControl::draw_text_range(HDC dc, std::size_t begin, const std::size_t end,
                                   int x, const int y, const COLORREF foreground,
                                   const COLORREF background) noexcept {
    return draw_buffer_range(dc, document_.text(), begin, end, x, y,
                             foreground, background);
}

int EditorControl::draw_buffer_range(HDC dc,
    const document::SecureGapBuffer& text, std::size_t begin, const std::size_t end,
    int x, const int y, const COLORREF foreground, const COLORREF background) noexcept {
    if (end <= begin || !scratch_.valid()) return 0;
    auto* buffer = reinterpret_cast<char16_t*>(scratch_.data());
    const std::size_t capacity = scratch_.size() / sizeof(char16_t);
    const std::size_t input_capacity = std::max<std::size_t>(1U, capacity / 4U);
    int total = 0;
    SetTextColor(dc, foreground);
    SetBkColor(dc, background);
    for_each_font_run(text, dc, font_, cjk_font_, begin, end,
        [&](const std::size_t run_begin, const std::size_t run_end) {
            std::size_t position = run_begin;
            while (position < run_end) {
                const std::size_t count = std::min(input_capacity, run_end - position);
                if (!text.copy_range(position, position + count,
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
                position += count;
                x += extent.cx;
                total += extent.cx;
            }
        });
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
    const COLORREF plain_foreground = editor_text(dark_theme_);
    const COLORREF selected_foreground = selection_text(dark_theme_);
    const COLORREF selected_background = selection_background(dark_theme_);
    const COLORREF normal_background = editor_background(dark_theme_);
    int x = 4 + start_pixel - horizontal_offset_;
    const auto draw_segment = [&](const std::size_t begin, const std::size_t stop,
                                  const COLORREF foreground,
                                  const COLORREF foreground_selected) {
        if (stop <= begin) return;
        const std::size_t selected_begin = std::clamp(select_begin, begin, stop);
        const std::size_t selected_end = std::clamp(select_end, begin, stop);
        x += draw_text_range(dc, begin, selected_begin, x, y,
                             foreground, normal_background);
        x += draw_text_range(dc, selected_begin, selected_end, x, y,
                             foreground_selected, selected_background);
        x += draw_text_range(dc, selected_end, stop, x, y,
                             foreground, normal_background);
    };

    const bool highlighted = syntax_highlighting_ &&
        highlight::path_is_highlightable(document_.path());
    if (highlighted) {
        const bool is_ini = highlight::path_is_ini(document_.path());
        const std::size_t start = lines_.line_start(line);
        highlight::Token tokens[max_highlight_tokens]{};
        const std::size_t token_count = highlight::tokenize_line(
            document_.text(), start, end, is_ini, tokens, max_highlight_tokens);
        std::size_t cursor = visible_start;
        for (std::size_t index = 0; index < token_count; ++index) {
            const std::size_t token_begin = std::max(tokens[index].begin, visible_start);
            const std::size_t token_end = std::min(tokens[index].end, visible_end);
            if (cursor < token_begin) {
                draw_segment(cursor, token_begin, plain_foreground, selected_foreground);
            }
            if (token_end > token_begin) {
                const COLORREF color =
                    highlight_color(tokens[index].kind, dark_theme_);
                draw_segment(token_begin, token_end, color, color);
            }
            if (token_end > cursor) cursor = token_end;
        }
        if (cursor < visible_end) {
            draw_segment(cursor, visible_end, plain_foreground, selected_foreground);
        }
    } else {
        draw_segment(visible_start, visible_end, plain_foreground, selected_foreground);
    }
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
    const int content_bottom = editor_bottom();
    const int saved = SaveDC(dc);
    IntersectClipRect(dc, 0, 0, client_width_, content_bottom);
    const std::size_t visible_lines = static_cast<std::size_t>(
        std::max(1, (content_bottom + line_height_ - 1) / line_height_));
    HBRUSH background = CreateSolidBrush(editor_background(dark_theme_));
    for (std::size_t row = 0; row < visible_lines; ++row) {
        const int y = static_cast<int>(row) * line_height_;
        const int bottom = std::min(content_bottom, y + line_height_);
        if (bottom <= paint.rcPaint.top || y >= paint.rcPaint.bottom) continue;
        if (first_line_ + row < lines_.line_count()) {
            draw_line(dc, first_line_ + row, y);
        } else {
            const RECT empty{0, y, client.right, bottom};
            FillRect(dc, &empty, background);
        }
    }
    DeleteObject(background);
    RestoreDC(dc, saved);
    if (find_visible_) draw_find_bar(dc);

    const int status_top = std::max(0, client_height_ - status_height);
    RECT status{0, status_top, client.right, client.bottom};
    HBRUSH status_brush = CreateSolidBrush(status_background(dark_theme_));
    FillRect(dc, &status, status_brush);
    DeleteObject(status_brush);
    SelectObject(dc, ui_font_);
    SetBkMode(dc, TRANSPARENT);
    HPEN status_separator = CreatePen(PS_SOLID, 1, status_border(dark_theme_));
    const HGDIOBJ previous_pen = SelectObject(dc, status_separator);
    MoveToEx(dc, 0, status_top, nullptr);
    LineTo(dc, client.right, status_top);
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
    ExtTextOutW(dc, 8, status_top + 5, ETO_CLIPPED, &status, status_text,
                static_cast<UINT>(std::wcslen(status_text)), nullptr);
    SelectObject(dc, previous);
    EndPaint(window_, &paint);
}

void EditorControl::update_caret() noexcept {
    if (!initialized_ || window_ == nullptr || !document_.open()) {
        focus(false);
        return;
    }
    if (find_focused_) {
        update_find_caret();
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
        std::max(1, editor_bottom() / line_height_));
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
    if (find_focused_) {
        insert_text({});
        return;
    }
    if (!selection_.empty() && document_.erase(selection_.begin(), selection_.end())) {
        selection_.collapse(selection_.begin());
        rebuild();
        notify_change();
    }
}

void EditorControl::insert_text(const std::span<const char16_t> text) noexcept {
    if (find_focused_) {
        const std::size_t begin = find_selection_.begin();
        const std::size_t end = find_selection_.end();
        if (text.size() > max_query_units - (find_query_.size() - (end - begin)) ||
            std::any_of(text.begin(), text.end(), [](const char16_t value) {
                return value < u' ' || value == 0x7FU || value == 0x85U ||
                       value == 0x2028U || value == 0x2029U;
            })) {
            MessageBoxW(window_, L"查找文本须为单行，且不超过 1024 个 UTF-16 单元。",
                        L"安全编辑器", MB_OK | MB_ICONINFORMATION);
            return;
        }
        if (!find_query_.replace(begin, end, text)) {
            MessageBoxW(window_, L"无法为查找文本分配安全内存。", L"安全编辑器",
                        MB_OK | MB_ICONERROR);
            return;
        }
        find_selection_.collapse(begin + text.size());
        refresh_find(selection_.begin());
        return;
    }
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

bool EditorControl::copy_selection_to_clipboard_impl() noexcept {
    const Selection& selected = active_selection();
    const auto& text = active_text();
    if (!document_.open() || selected.empty()) return false;

    const std::size_t begin = selected.begin();
    const std::size_t end = selected.end();
    std::size_t output_units = 1U; // NUL terminator
    for (std::size_t position = begin; position < end; ++position) {
        const std::size_t added = text.at(position) == u'\n' ? 2U : 1U;
        if (output_units > std::numeric_limits<std::size_t>::max() - added) {
            MessageBoxW(window_, L"无法复制到剪贴板。", L"安全编辑器",
                        MB_OK | MB_ICONERROR);
            return false;
        }
        output_units += added;
    }
    if (output_units > std::numeric_limits<std::size_t>::max() / sizeof(wchar_t)) {
        MessageBoxW(window_, L"无法复制到剪贴板。", L"安全编辑器",
                    MB_OK | MB_ICONERROR);
        return false;
    }

    const std::size_t output_bytes = output_units * sizeof(wchar_t);
    HGLOBAL clipboard_memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT,
                                           output_bytes);
    if (clipboard_memory == nullptr) {
        MessageBoxW(window_, L"无法复制到剪贴板。", L"安全编辑器",
                    MB_OK | MB_ICONERROR);
        return false;
    }
    auto* output = static_cast<wchar_t*>(GlobalLock(clipboard_memory));
    if (output == nullptr) {
        GlobalFree(clipboard_memory);
        MessageBoxW(window_, L"无法复制到剪贴板。", L"安全编辑器",
                    MB_OK | MB_ICONERROR);
        return false;
    }

    std::size_t write = 0;
    for (std::size_t position = begin; position < end; ++position) {
        const char16_t value = text.at(position);
        if (value == u'\n') output[write++] = L'\r';
        output[write++] = static_cast<wchar_t>(value);
    }
    output[write] = L'\0';
    GlobalUnlock(clipboard_memory);

    bool transferred = false;
    if (OpenClipboard(window_) != FALSE) {
        if (EmptyClipboard() != FALSE &&
            SetClipboardData(CF_UNICODETEXT, clipboard_memory) != nullptr) {
            transferred = true;
        }
        CloseClipboard();
    }
    if (transferred) return true;

    output = static_cast<wchar_t*>(GlobalLock(clipboard_memory));
    if (output != nullptr) {
        security::secure_zero(output, output_bytes);
        GlobalUnlock(clipboard_memory);
    }
    GlobalFree(clipboard_memory);
    MessageBoxW(window_, L"无法复制到剪贴板。", L"安全编辑器",
                MB_OK | MB_ICONERROR);
    return false;
}

void EditorControl::copy_selection_to_clipboard() noexcept {
    (void)copy_selection_to_clipboard_impl();
}

void EditorControl::cut_selection_to_clipboard() noexcept {
    if (copy_selection_to_clipboard_impl()) delete_selection();
}

void EditorControl::paste_from_clipboard() noexcept {
    if (!document_.open()) return;
    security::SecureAllocation clipboard_text;
    std::size_t units = 0;
    const wchar_t* error = nullptr;
    if (!read_clipboard_text(window_, clipboard_text, units, error)) {
        MessageBoxW(window_, error, L"安全编辑器", MB_OK | MB_ICONERROR);
        return;
    }
    if (units == 0) return;
    insert_text(std::span<const char16_t>(
        reinterpret_cast<const char16_t*>(clipboard_text.data()), units));
}

void EditorControl::select_all() noexcept {
    if (!document_.open()) return;
    active_selection().set(0, active_text().size());
    ensure_caret_visible();
}

Selection& EditorControl::active_selection() noexcept {
    return find_focused_ ? find_selection_ : selection_;
}

const document::SecureGapBuffer& EditorControl::active_text() const noexcept {
    return find_focused_ ? find_query_ : document_.text();
}

int EditorControl::editor_bottom() const noexcept {
    const int find_height = find_visible_ ? 2 * line_height_ + 16 : 0;
    return std::max(0, client_height_ - status_height - find_height);
}

RECT EditorControl::find_input_rect() const noexcept {
    const int left = std::min(160, std::max(0, client_width_ / 3));
    return {left, editor_bottom() + 4, std::max(left, client_width_ - 8),
            editor_bottom() + line_height_ + 8};
}

void EditorControl::show_find() noexcept {
    if (!document_.open()) return;
    mouse_up();
    security::secure_zero(input_.data(), input_.size());
    // Seed from a short single-line selection using only the secure scratch.
    const std::size_t units = selection_.end() - selection_.begin();
    bool seeded = false;
    if (!find_focused_ && units != 0 && units <= max_query_units) {
        auto* buffer = reinterpret_cast<char16_t*>(scratch_.data());
        const std::span<char16_t> text(buffer, units);
        if (document_.text().copy_range(selection_.begin(), selection_.end(), text) &&
            std::none_of(text.begin(), text.end(), [](const char16_t value) {
                return value < u' ' || value == 0x7FU || value == 0x85U ||
                       value == 0x2028U || value == 0x2029U;
            })) {
            if (!find_query_.assign(text)) {
                security::secure_zero(buffer, units * sizeof(char16_t));
                close_find();
                MessageBoxW(window_, L"无法为查找文本分配安全内存。", L"安全编辑器",
                            MB_OK | MB_ICONERROR);
                return;
            }
            seeded = true;
        }
        security::secure_zero(buffer, units * sizeof(char16_t));
    }
    find_visible_ = true;
    find_focused_ = true;
    find_selection_.set(0, find_query_.size());
    SetFocus(window_);
    if (seeded || !find_query_.empty()) refresh_find(selection_.begin());
    else ensure_caret_visible();
}

void EditorControl::close_find(const bool update_view) noexcept {
    const bool was_visible = find_visible_;
    if (was_visible) mouse_up();
    find_query_.clear();
    find_selection_.collapse(0);
    find_result_ = {};
    find_visible_ = false;
    find_focused_ = false;
    find_offset_ = 0;
    suppress_tab_character_ = false;
    if (input_.valid()) security::secure_zero(input_.data(), input_.size());
    if (update_view && was_visible && initialized_ && window_ != nullptr) ensure_caret_visible();
}

void EditorControl::refresh_find(const std::size_t origin, const bool backwards) noexcept {
    find_result_ = find_literal(document_.text(), find_query_, origin, backwards);
    if (find_result_.count != 0) {
        selection_.set(find_result_.range.begin, find_result_.range.end);
        const std::size_t line = lines_.line_for_position(selection_.caret());
        preferred_column_ = selection_.caret() - lines_.line_start(line);
    }
    ensure_caret_visible();
}

void EditorControl::find_next(const bool backwards) noexcept {
    if (!document_.open()) return;
    if (!find_visible_ || find_query_.empty()) { show_find(); return; }
    // Advancing one scalar also permits overlapping matches. size()+1 forces
    // wrap when the caret is already at EOF.
    const std::size_t origin = backwards ? selection_.begin()
        : (selection_.begin() < document_.text().size()
            ? document_.text().next_scalar(selection_.begin())
            : document_.text().size() + 1U);
    refresh_find(origin, backwards);
}

void EditorControl::update_find_caret() noexcept {
    const RECT input = find_input_rect();
    HDC dc = GetDC(window_);
    if (dc == nullptr) return;
    const int saved = SaveDC(dc);
    const int pixels = measure_buffer_range(dc, find_query_, 0, find_selection_.caret());
    const int available = std::max(1L, input.right - input.left - 8);
    if (pixels < find_offset_) find_offset_ = pixels;
    else if (pixels > find_offset_ + available) find_offset_ = pixels - available;
    SetCaretPos(input.left + 4 + pixels - find_offset_, input.top + 2);
    RestoreDC(dc, saved);
    ReleaseDC(window_, dc);
}

std::size_t EditorControl::find_position_for_pixel(const int x) noexcept {
    HDC dc = GetDC(window_);
    if (dc == nullptr) return 0;
    const int saved = SaveDC(dc);
    const int target = std::max(0L, x - find_input_rect().left - 4 + find_offset_);
    std::size_t position = 0;
    int pixels = 0;
    while (position < find_query_.size()) {
        const std::size_t next = find_query_.next_scalar(position);
        const int advance = measure_buffer_range(dc, find_query_, position, next);
        if (target < pixels + (advance + 1) / 2) break;
        pixels += advance;
        position = next;
    }
    RestoreDC(dc, saved);
    ReleaseDC(window_, dc);
    return position;
}

void EditorControl::draw_find_bar(HDC dc) noexcept {
    const int saved = SaveDC(dc);
    const RECT bar{0, editor_bottom(), client_width_,
                   std::max(0, client_height_ - status_height)};
    IntersectClipRect(dc, bar.left, bar.top, bar.right, bar.bottom);
    HBRUSH background = CreateSolidBrush(status_background(dark_theme_));
    FillRect(dc, &bar, background);
    DeleteObject(background);
    const RECT input = find_input_rect();
    SelectObject(dc, ui_font_);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, status_text_color(dark_theme_));
    RECT label{8, input.top, input.left - 4, input.bottom};
    DrawTextW(dc, L"查找（区分大小写）", -1, &label,
              DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    HBRUSH field = CreateSolidBrush(editor_background(dark_theme_));
    FillRect(dc, &input, field);
    DeleteObject(field);
    HBRUSH border = CreateSolidBrush(status_border(dark_theme_));
    FrameRect(dc, &input, border);
    DeleteObject(border);
    const int field_saved = SaveDC(dc);
    IntersectClipRect(dc, input.left + 2, input.top + 1, input.right - 2, input.bottom - 1);
    SetBkMode(dc, OPAQUE);
    const std::size_t begin = find_selection_.begin();
    const std::size_t end = find_selection_.end();
    int x = input.left + 4 - find_offset_;
    x += draw_buffer_range(dc, find_query_, 0, begin, x, input.top + 2,
                           editor_text(dark_theme_), editor_background(dark_theme_));
    x += draw_buffer_range(dc, find_query_, begin, end, x, input.top + 2,
                           selection_text(dark_theme_), selection_background(dark_theme_));
    draw_buffer_range(dc, find_query_, end, find_query_.size(), x, input.top + 2,
                       editor_text(dark_theme_), editor_background(dark_theme_));
    RestoreDC(dc, field_saved);
    wchar_t result[96]{};
    if (find_query_.empty()) {
        std::swprintf(result, std::size(result), L"输入关键词；Esc 关闭");
    } else if (find_result_.count == 0) {
        std::swprintf(result, std::size(result), L"未找到匹配项");
    } else if (find_result_.ordinal == 0) {
        std::swprintf(result, std::size(result), L"共 %zu 项", find_result_.count);
    } else {
        std::swprintf(result, std::size(result), L"%zu / %zu%ls",
                      find_result_.ordinal, find_result_.count,
                      find_result_.wrapped ? L"（已循环）" : L"");
    }
    const int button_width = std::min(88, std::max(1, client_width_ / 4));
    RECT result_area{8, input.bottom + 2, client_width_ - 3 * button_width,
                     bar.bottom - 2};
    DrawTextW(dc, result, -1, &result_area, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    const wchar_t* buttons[]{L"上一个 ↑", L"下一个 ↓", L"关闭 ×"};
    for (int index = 0; index < 3; ++index) {
        RECT button{client_width_ - (3 - index) * button_width, input.bottom + 2,
                     client_width_ - (2 - index) * button_width - 4, bar.bottom - 2};
        DrawTextW(dc, buttons[index], -1, &button, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    }
    RestoreDC(dc, saved);
}

void EditorControl::key_down(const WPARAM key, const bool shift,
                             const bool control) noexcept {
    if (!document_.open()) return;
    if (control) {
        switch (key) {
        case 'F': show_find(); return;
        case 'A': select_all(); return;
        case 'C': copy_selection_to_clipboard(); return;
        case 'V': paste_from_clipboard(); return;
        case 'X': cut_selection_to_clipboard(); return;
        default: break;
        }
    }
    if (key == VK_F3) { find_next(shift); return; }
    if (key == VK_ESCAPE && find_visible_) { close_find(); return; }
    if (find_focused_) {
        std::size_t target = find_selection_.caret();
        switch (key) {
        case VK_RETURN: find_next(shift); return;
        case VK_TAB:
            find_focused_ = false;
            suppress_tab_character_ = true;
            security::secure_zero(input_.data(), input_.size());
            ensure_caret_visible();
            return;
        case VK_LEFT:
            target = !shift && !find_selection_.empty() ? find_selection_.begin()
                : find_query_.previous_scalar(target);
            break;
        case VK_RIGHT:
            target = !shift && !find_selection_.empty() ? find_selection_.end()
                : find_query_.next_scalar(target);
            break;
        case VK_HOME: target = 0; break;
        case VK_END: target = find_query_.size(); break;
        case VK_BACK:
            if (find_selection_.empty()) find_selection_.extend(find_query_.previous_scalar(target));
            delete_selection(); return;
        case VK_DELETE:
            if (find_selection_.empty()) find_selection_.extend(find_query_.next_scalar(target));
            delete_selection(); return;
        default: return;
        }
        if (shift) find_selection_.extend(target); else find_selection_.collapse(target);
        update_caret();
        InvalidateRect(window_, nullptr, FALSE);
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
        move_vertical(-std::max(1, editor_bottom() /
                                   line_height_), shift); break;
    case VK_NEXT:
        move_vertical(std::max(1, editor_bottom() /
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
    if (value == u'\t' && suppress_tab_character_) {
        suppress_tab_character_ = false;
        return;
    }
    suppress_tab_character_ = false;
    // Enter/Tab are handled by WM_KEYDOWN while the find input is active.
    if (find_focused_ && (value == u'\r' || value == u'\t')) return;
    if (value == u'\t' && (GetKeyState(VK_CONTROL) & 0x8000) != 0) return;
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

std::size_t EditorControl::point_to_position(const int x, const int y,
                                            const bool nearest) noexcept {
    const std::size_t line = std::min(first_line_ + static_cast<std::size_t>(
        std::max(0, y) / line_height_), lines_.line_count() - 1U);
    HDC dc = GetDC(window_);
    if (dc == nullptr) return lines_.line_start(line);
    const HGDIOBJ previous = SelectObject(dc, font_);
    int position_pixel = 0;
    const int target = horizontal_offset_ + std::max(0, x - 4);
    const std::size_t position = position_for_pixel(
        dc, line, target, nearest, position_pixel);
    SelectObject(dc, previous);
    ReleaseDC(window_, dc);
    return position;
}

void EditorControl::mouse_down(const int x, const int y, const bool shift) noexcept {
    if (!document_.open() || y < 0) return;
    if (find_visible_ && y >= editor_bottom()) {
        const RECT input = find_input_rect();
        if (y >= input.top && y < input.bottom && x >= input.left && x < input.right) {
            SetFocus(window_);
            find_focused_ = true;
            security::secure_zero(input_.data(), input_.size());
            const std::size_t position = find_position_for_pixel(x);
            if (shift) find_selection_.extend(position); else find_selection_.collapse(position);
            mouse_selecting_ = true;
            word_selecting_ = false;
            SetCapture(window_);
            update_caret();
            InvalidateRect(window_, nullptr, FALSE);
        } else if (y >= input.bottom && y < client_height_ - status_height) {
            const int button_width = std::min(88, std::max(1, client_width_ / 4));
            if (x >= client_width_ - button_width) close_find();
            else if (x >= client_width_ - button_width * 2) find_next();
            else if (x >= client_width_ - button_width * 3) find_next(true);
        }
        return;
    }
    if (y >= editor_bottom()) return;
    SetFocus(window_);
    find_focused_ = false;
    security::secure_zero(input_.data(), input_.size());
    const std::size_t position = point_to_position(x, y);
    if (shift) selection_.extend(position); else selection_.collapse(position);
    mouse_selecting_ = true;
    word_selecting_ = false;
    SetCapture(window_);
    ensure_caret_visible();
}

void EditorControl::mouse_move(const int x, const int y, const bool left_button) noexcept {
    if (mouse_selecting_ && left_button) {
        if (find_focused_) {
            find_selection_.extend(find_position_for_pixel(x));
            update_caret();
            InvalidateRect(window_, nullptr, FALSE);
            return;
        }
        if (word_selecting_) {
            const TextRange word = word_at(document_.text(), point_to_position(x, y, false));
            if (word.begin < word_anchor_.begin) selection_.set(word_anchor_.end, word.begin);
            else selection_.set(word_anchor_.begin, std::max(word_anchor_.end, word.end));
        } else {
            selection_.extend(point_to_position(x, y));
        }
        ensure_caret_visible();
    }
}

void EditorControl::mouse_up() noexcept {
    const bool captured = mouse_selecting_ && GetCapture() == window_;
    mouse_selecting_ = false;
    word_selecting_ = false;
    if (captured) ReleaseCapture();
}

void EditorControl::mouse_double_click(const int x, const int y) noexcept {
    if (!document_.open()) return;
    if (find_visible_ && y >= editor_bottom()) {
        const RECT input = find_input_rect();
        if (y >= input.top && y < input.bottom && x >= input.left && x < input.right) {
            find_focused_ = true;
            select_all();
        }
        return;
    }
    if (x < 4 || y < 0 || y >= editor_bottom()) return;
    const std::size_t line = first_line_ + static_cast<std::size_t>(y / line_height_);
    if (line >= lines_.line_count()) return;
    const std::size_t position = point_to_position(x, y, false);
    // Clicking in the blank area after a line does not select its last word.
    if (position >= lines_.line_end(line, document_.text())) return;
    SetFocus(window_);
    find_focused_ = false;
    security::secure_zero(input_.data(), input_.size());
    word_anchor_ = word_at(document_.text(), position);
    selection_.set(word_anchor_.begin, word_anchor_.end);
    preferred_column_ = selection_.caret() - lines_.line_start(line);
    mouse_selecting_ = true;
    word_selecting_ = true;
    SetCapture(window_);
    ensure_caret_visible();
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

void EditorControl::set_font(const FontPreset preset) noexcept {
    if (preset == font_preset_) return;
    font_preset_ = preset;
    apply_font_preset();
}

void EditorControl::set_syntax_highlighting(const bool enabled) noexcept {
    if (syntax_highlighting_ == enabled) return;
    syntax_highlighting_ = enabled;
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
    if (find_visible_) {
        find_result_ = find_literal(document_.text(), find_query_, selection_.begin(), false);
        if (find_result_.range.begin != selection_.begin() ||
            find_result_.range.end != selection_.end()) find_result_.ordinal = 0;
    }
    const std::size_t line = lines_.line_for_position(selection_.caret());
    preferred_column_ = selection_.caret() - lines_.line_start(line);
    ensure_caret_visible();
    PostMessageW(window_, change_message, 0, 0);
}

} // namespace mempad::editor
