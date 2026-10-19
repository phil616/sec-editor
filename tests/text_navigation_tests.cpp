#include "test_support.h"

#include "editor/text_navigation.h"

#include <string>
#include <string_view>

namespace {
using namespace mempad;

void check_word(const document::SecureGapBuffer& text, const std::size_t position,
                const std::size_t begin, const std::size_t end) {
    const auto range = editor::word_at(text, position);
    CHECK(range.begin == begin);
    CHECK(range.end == end);
}

void check_match(const editor::SearchResult& result, const std::size_t begin,
                 const std::size_t end, const std::size_t ordinal,
                 const std::size_t count, const bool wrapped = false) {
    CHECK(result.range.begin == begin);
    CHECK(result.range.end == end);
    CHECK(result.ordinal == ordinal);
    CHECK(result.count == count);
    CHECK(result.wrapped == wrapped);
}
}

void run_text_navigation_tests() {
    using namespace mempad;
    document::SecureGapBuffer text;
    CHECK(text.assign(std::u16string_view{u"/etc/ssh/sshd_config"}));
    for (std::size_t position = 5; position < 8; ++position) check_word(text, position, 5, 8);
    for (std::size_t position = 9; position < 20; ++position) check_word(text, position, 9, 20);
    check_word(text, 0, 0, 1);
    check_word(text, 8, 8, 9);
    check_word(text, text.size(), text.size(), text.size());
    check_word(text, text.size() + 10U, text.size(), text.size());

    CHECK(text.assign(std::u16string_view{u"abc.def-ghi\\jkl  \t\n\n中文_12\u3000e\u0301 \U00010400x!\U0001F600"}));
    check_word(text, 0, 0, 3);
    check_word(text, 3, 3, 4);
    check_word(text, 7, 7, 8);
    check_word(text, 11, 11, 12);
    check_word(text, 15, 15, 18);
    check_word(text, 18, 18, 18);
    check_word(text, 19, 19, 19);
    check_word(text, 21, 20, 25);
    check_word(text, 25, 25, 26);
    check_word(text, 27, 26, 28); // combining accent stays with its base
    check_word(text, 29, 29, 32); // supplementary letter + ASCII letter
    check_word(text, 30, 29, 32); // low surrogate normalizes to scalar start
    check_word(text, 34, 32, 35); // punctuation + emoji, never half a surrogate
    CHECK(text.erase(4, 7)); // leave the gap inside the document
    check_word(text, 4, 3, 5); // adjacent punctuation after the erase
    text.clear();
    check_word(text, 0, 0, 0);

    document::SecureGapBuffer query;
    CHECK(text.assign(std::u16string_view{u"ssh sshd SSH ssh"}));
    CHECK(query.assign(std::u16string_view{u"ssh"}));
    check_match(editor::find_literal(text, query, 0, false), 0, 3, 1, 3);
    check_match(editor::find_literal(text, query, 1, false), 4, 7, 2, 3);
    check_match(editor::find_literal(text, query, 5, false), 13, 16, 3, 3);
    check_match(editor::find_literal(text, query, 14, false), 0, 3, 1, 3, true);
    check_match(editor::find_literal(text, query, 13, true), 4, 7, 2, 3);
    check_match(editor::find_literal(text, query, 0, true), 13, 16, 3, 3, true);
    check_match(editor::find_literal(text, query, 100, true), 13, 16, 3, 3);
    CHECK(text.insert(6, std::u16string_view{u"X"}));
    check_match(editor::find_literal(text, query, 1, false), 14, 17, 2, 2);
    CHECK(text.erase(6, 7));
    check_match(editor::find_literal(text, query, 1, false), 4, 7, 2, 3);

    CHECK(text.assign(std::u16string_view{u"aaaaa"}));
    CHECK(query.assign(std::u16string_view{u"aaa"}));
    check_match(editor::find_literal(text, query, 1, false), 1, 4, 2, 3);
    check_match(editor::find_literal(text, query, 1, true), 0, 3, 1, 3);
    CHECK(query.assign(std::u16string_view{u"aaaaab"}));
    CHECK(editor::find_literal(text, query, 0, false).count == 0);
    CHECK(query.assign(std::u16string_view{u"z"}));
    CHECK(editor::find_literal(text, query, 0, true).ordinal == 0);
    query.clear();
    CHECK(editor::find_literal(text, query, 0, false).count == 0);

    CHECK(text.assign(std::u16string_view{u"中文\U0001F600\n中文\U0001F600"}));
    CHECK(query.assign(std::u16string_view{u"文\U0001F600"}));
    check_match(editor::find_literal(text, query, 0, false), 1, 4, 1, 2);
    check_match(editor::find_literal(text, query, 2, false), 6, 9, 2, 2);
    CHECK(query.assign(std::u16string_view{u"\U0001F600\n中"}));
    check_match(editor::find_literal(text, query, 0, false), 2, 6, 1, 1);

    // Adversarial repetitive input exercises KMP's fallback path.
    std::u16string repeated(100000, u'a');
    repeated += u'b';
    CHECK(text.assign(repeated));
    CHECK(query.assign(std::u16string_view{u"aaaaab"}));
    check_match(editor::find_literal(text, query, 0, false), 99995, 100001, 1, 1);
    text.clear();
    CHECK(editor::find_literal(text, query, 0, false).count == 0);
}
