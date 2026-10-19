#include "test_support.h"

#include "editor/editor_control.h"
#include "security/process_guard.h"

#include <string_view>

namespace {
using namespace mempad;

constexpr std::u16string_view sample = u"/etc/ssh/sshd_config\nssh\n中文\U0001F600";

bool equals(const document::SecureDocument& document, const std::u16string_view expected) {
    if (document.text().size() != expected.size()) return false;
    for (std::size_t index = 0; index < expected.size(); ++index) {
        if (document.text().at(index) != expected[index]) return false;
    }
    return true;
}

void load(document::SecureDocument& document, editor::EditorControl& editor) {
    CHECK(document.load(sample, encoding::TextEncoding::utf8, {}, L"test.conf"));
    editor.document_changed();
}

void type(editor::EditorControl& editor, const std::u16string_view text) {
    for (const char16_t value : text) editor.character(value);
}
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    CHECK(security::initialize_process_guard());
    WNDCLASSW window_class{};
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpfnWndProc = DefWindowProcW;
    window_class.lpszClassName = L"MempadEditorTestWindow";
    CHECK(RegisterClassW(&window_class) != 0);
    // Remains hidden; does not send global keyboard input or use the clipboard.
    HWND window = CreateWindowW(window_class.lpszClassName, L"", WS_OVERLAPPEDWINDOW,
                                 0, 0, 900, 650, nullptr, nullptr,
                                 window_class.hInstance, nullptr);
    CHECK(window != nullptr);
    if (window == nullptr) return 1;
    document::SecureDocument document;
    editor::EditorControl editor(document);
    const bool initialized = editor.initialize(window);
    CHECK(initialized);
    if (!initialized) { DestroyWindow(window); return 1; }
    editor.resize(900, 600);
    load(document, editor);

    editor.key_down('F', false, true);
    type(editor, u"ssh");
    CHECK(editor.caret() == 8);
    CHECK(!document.dirty());
    CHECK(equals(document, sample));
    editor.key_down(VK_RETURN, false, false);
    editor.character(u'\r'); // the translated WM_CHAR must not edit either input
    CHECK(editor.caret() == 12);
    CHECK(equals(document, sample));
    editor.key_down(VK_F3, false, false);
    CHECK(editor.caret() == 24);
    editor.key_down(VK_F3, false, false);
    CHECK(editor.caret() == 8); // wraps
    editor.key_down(VK_RETURN, true, false);
    CHECK(editor.caret() == 24); // backward wraps
    editor.key_down(VK_F3, true, false);
    CHECK(editor.caret() == 12);
    editor.key_down('A', false, true); // selects query, not the document
    type(editor, u"missing");
    CHECK(editor.caret() == 12);
    CHECK(!document.dirty());
    editor.key_down('F', false, true); // keep query when already editing it
    type(editor, u"ssh");
    CHECK(editor.caret() == 12);
    editor.key_down(VK_ESCAPE, false, false);
    type(editor, u"Q");
    CHECK(equals(document, u"/etc/ssh/Qd_config\nssh\n中文\U0001F600"));

    load(document, editor);
    editor.key_down('F', false, true);
    type(editor, u"ssh");
    editor.key_down(VK_TAB, false, false);
    editor.character(u'\t');
    CHECK(equals(document, sample));
    type(editor, u"Q");
    CHECK(equals(document, u"/etc/Q/sshd_config\nssh\n中文\U0001F600"));

    load(document, editor);
    editor.key_down('F', false, true);
    type(editor, u"ssh");
    editor.key_down('A', false, true);
    type(editor, u"中文\U0001F600");
    CHECK(editor.caret() == sample.size());
    editor.key_down(VK_BACK, false, false); // removes entire surrogate pair
    CHECK(editor.caret() == sample.size() - 2U);
    CHECK(equals(document, sample));

    // Replacing a document must also close and clear its find input.
    load(document, editor);
    type(editor, u"Q");
    CHECK(document.text().at(0) == u'Q');

    // Use the editor's default font metrics to click both halves of word edges.
    HDC dc = GetDC(window);
    HFONT font = CreateFontW(-MulDiv(12, GetDeviceCaps(dc, LOGPIXELSY), 72),
        0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        FIXED_PITCH | FF_MODERN, L"Consolas");
    const auto previous = SelectObject(dc, font);
    SIZE start{}, end{};
    GetTextExtentPoint32W(dc, L"/etc/", 5, &start);
    GetTextExtentPoint32W(dc, L"/etc/ssh", 8, &end);
    for (const int x : {4 + start.cx + 1, 4 + end.cx - 1}) {
        load(document, editor);
        editor.mouse_double_click(x, 2);
        CHECK(editor.caret() == 8);
        editor.mouse_up();
        type(editor, u"Q");
        CHECK(equals(document, u"/etc/Q/sshd_config\nssh\n中文\U0001F600"));
    }
    load(document, editor);
    editor.mouse_double_click(4 + end.cx - 1, 2);
    editor.mouse_move(4 + end.cx + 20, 2, true); // extend through next word
    editor.mouse_up();
    CHECK(editor.caret() == 20);
    type(editor, u"Q");
    CHECK(equals(document, u"/etc/Q\nssh\n中文\U0001F600"));
    SelectObject(dc, previous);
    DeleteObject(font);
    ReleaseDC(window, dc);

    // Paint and resize the open bar, then exercise the session-clear path.
    load(document, editor);
    editor.show_find();
    type(editor, u"ssh");
    for (const int width : {900, 240, 20}) {
        editor.resize(width, 600);
        InvalidateRect(window, nullptr, FALSE);
        editor.paint();
    }
    document.close();
    editor.document_changed();
    CHECK(document.create_empty());
    editor.document_changed();
    type(editor, u"Q");
    CHECK(equals(document, u"Q"));

    editor.shutdown();
    DestroyWindow(window);
    std::printf("%d checks, %d failures\n", tests::checks, tests::failures);
    return tests::failures == 0 ? 0 : 1;
}
