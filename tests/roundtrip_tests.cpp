#include "test_support.h"

#include "encoding/encoding.h"

#include <cstddef>
#include <string>
#include <vector>

namespace {
void roundtrip(const std::u16string& text, const mempad::encoding::TextEncoding encoding,
               const mempad::encoding::NewlineKind newline) {
    using namespace mempad::encoding;
    std::size_t bytes = 0;
    CHECK(encoded_size(text, encoding, newline, bytes));
    std::vector<std::byte> output(bytes);
    std::size_t written = 0;
    CHECK(encode(text, encoding, newline, output, written));
    CHECK(written == bytes);
    const Analysis info = analyze(output);
    CHECK(info.ok);
    CHECK(info.utf16_units == text.size());
    std::u16string decoded(info.utf16_units, u'\0');
    CHECK(decode(output, info, decoded));
    CHECK(decoded == text);
}
}

void run_roundtrip_tests() {
    using namespace mempad::encoding;
    const std::u16string sample = u"ASCII 中文 日本語 \U0001F600\nsecond";
    roundtrip(sample, TextEncoding::utf8, NewlineKind::lf);
    roundtrip(sample, TextEncoding::utf8_bom, NewlineKind::crlf);
    roundtrip(sample, TextEncoding::utf16_le, NewlineKind::cr);
    roundtrip(sample, TextEncoding::utf16_be, NewlineKind::crlf);
    roundtrip(u"", TextEncoding::utf8_bom, NewlineKind::crlf);

    std::vector<std::byte> near_limit(8U * 1024U * 1024U - 1U, std::byte{'a'});
    const Analysis result = analyze(near_limit);
    CHECK(result.ok);
    CHECK(result.utf16_units == near_limit.size());
}
