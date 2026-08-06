#include "test_support.h"

#include "document/secure_document.h"
#include "editor/line_index.h"

#include <array>
#include <string_view>
#include <string>

void run_document_tests() {
    using namespace mempad;
    document::SecureDocument document;
    encoding::NewlineInfo newline{};
    CHECK(document.load(std::u16string_view{u"one\ntwo"}, encoding::TextEncoding::ascii, newline, L"input.txt"));
    CHECK(document.open() && document.secure() && !document.dirty());
    CHECK(document.insert(7, std::u16string_view{u"中"}));
    CHECK(document.encoding() == encoding::TextEncoding::utf8);
    CHECK(document.dirty());
    editor::LineIndex index;
    index.rebuild(document.text());
    CHECK(index.line_count() == 2);
    CHECK(index.line_for_position(5) == 1);
    CHECK(index.position(1, 1, document.text()) == 5);
    CHECK(document.erase(0, 4));
    index.rebuild(document.text());
    CHECK(index.line_count() == 1);
    document.close();
    CHECK(!document.open() && !document.text().secure());

    document::SecureDocument limited;
    std::u16string too_long(document::SecureDocument::max_line_units + 1U, u'x');
    CHECK(!limited.load(too_long, encoding::TextEncoding::ascii, newline, L"too-long.txt"));
    CHECK(!limited.open());
}
