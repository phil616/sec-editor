#include "test_support.h"

#include "encoding/encoding.h"

#include <array>
#include <cstddef>
#include <span>
#include <vector>

using mempad::encoding::TextEncoding;

namespace {
std::vector<std::byte> bytes(std::initializer_list<unsigned> values) {
    std::vector<std::byte> result;
    for (const unsigned value : values) result.push_back(static_cast<std::byte>(value));
    return result;
}
}

void run_encoding_tests() {
    using namespace mempad::encoding;
    const auto ascii = bytes({'a', 'b', 'c', '\r', '\n'});
    const Analysis ascii_info = analyze(ascii);
    CHECK(ascii_info.ok);
    CHECK(ascii_info.encoding == TextEncoding::ascii);
    CHECK(ascii_info.utf16_units == 4);
    CHECK(ascii_info.newline.selected == NewlineKind::crlf);

    const auto chinese = bytes({0xE4, 0xB8, 0xAD, 0xE6, 0x96, 0x87});
    const Analysis chinese_info = analyze(chinese);
    CHECK(chinese_info.ok);
    CHECK(chinese_info.encoding == TextEncoding::utf8);
    CHECK(chinese_info.utf16_units == 2);

    const auto emoji = bytes({0xF0, 0x9F, 0x98, 0x80});
    const Analysis emoji_info = analyze(emoji);
    CHECK(emoji_info.ok);
    CHECK(emoji_info.utf16_units == 2);
    std::array<char16_t, 2> decoded{};
    CHECK(decode(emoji, emoji_info, decoded));
    CHECK(decoded[0] == 0xD83D && decoded[1] == 0xDE00);

    CHECK(!analyze(bytes({0xC0, 0xAF})).ok);
    CHECK(!analyze(bytes({0xE2, 0x82})).ok);
    CHECK(!analyze(bytes({0xED, 0xA0, 0x80})).ok);
    CHECK(!analyze(bytes({0xF4, 0x90, 0x80, 0x80})).ok);

    const auto le = bytes({0xFF, 0xFE, 0x2D, 0x4E, 0x87, 0x65});
    const Analysis le_info = analyze(le);
    CHECK(le_info.ok && le_info.encoding == TextEncoding::utf16_le);
    CHECK(le_info.utf16_units == 2);
    const auto be = bytes({0xFE, 0xFF, 0x4E, 0x2D, 0x65, 0x87});
    CHECK(analyze(be).ok && analyze(be).encoding == TextEncoding::utf16_be);
    CHECK(!analyze(bytes({0xFF, 0xFE, 0x41})).ok);
    CHECK(!analyze(bytes({0xFF, 0xFE, 0x00, 0xD8, 0x41, 0x00})).ok);

    const auto mixed = bytes({'a', '\r', '\n', 'b', '\n', 'c', '\r'});
    const Analysis mixed_info = analyze(mixed);
    CHECK(mixed_info.ok);
    CHECK(mixed_info.newline.mixed);
    CHECK(mixed_info.newline.selected == NewlineKind::crlf);
    CHECK(mixed_info.utf16_units == 6);

    CHECK(analyze({}).ok);
    CHECK(analyze(bytes({0xEF, 0xBB, 0xBF})).ok);
    const Analysis le_bom_only = analyze(bytes({0xFF, 0xFE}));
    CHECK(le_bom_only.ok && le_bom_only.utf16_units == 0);
    const Analysis be_bom_only = analyze(bytes({0xFE, 0xFF}));
    CHECK(be_bom_only.ok && be_bom_only.utf16_units == 0);

    const Analysis cr_only = analyze(bytes({'a', '\r', 'b'}));
    CHECK(cr_only.ok && cr_only.newline.selected == NewlineKind::cr);
    const Analysis lf_only = analyze(bytes({'a', '\n', 'b'}));
    CHECK(lf_only.ok && lf_only.newline.selected == NewlineKind::lf);
}
