#include "test_support.h"

#include "editor/clipboard_text.h"

#include <array>
#include <string_view>

void run_clipboard_text_tests() {
    using namespace mempad::editor;

    constexpr std::u16string_view source = u"one\r\ntwo\rthree\nfour";
    constexpr std::u16string_view expected = u"one\ntwo\nthree\nfour";
    std::size_t size = 0;
    CHECK(clipboard_text_normalized_size(source, size));
    CHECK(size == expected.size());
    std::array<char16_t, expected.size()> output{};
    CHECK(normalize_clipboard_text(source, output));
    CHECK(std::u16string_view(output.data(), output.size()) == expected);

    constexpr std::array<char16_t, 2> scalar{0xD83D, 0xDE00};
    CHECK(clipboard_text_normalized_size(scalar, size));
    CHECK(size == scalar.size());
    std::array<char16_t, 2> scalar_output{};
    CHECK(normalize_clipboard_text(scalar, scalar_output));
    CHECK(scalar_output == scalar);

    constexpr std::array<char16_t, 1> lone_high{0xD83D};
    constexpr std::array<char16_t, 1> lone_low{0xDE00};
    CHECK(!clipboard_text_normalized_size(lone_high, size));
    CHECK(!clipboard_text_normalized_size(lone_low, size));

    std::array<char16_t, expected.size() - 1U> too_small{};
    CHECK(!normalize_clipboard_text(source, too_small));
}
