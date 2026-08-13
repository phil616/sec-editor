#include "test_support.h"

#include "document/secure_gap_buffer.h"
#include "editor/ini_env_highlight.h"

#include <array>
#include <initializer_list>
#include <span>
#include <string>

namespace {
using mempad::document::SecureGapBuffer;
using mempad::editor::highlight::Token;
using mempad::editor::highlight::TokenKind;
using mempad::editor::highlight::tokenize_line;

constexpr std::size_t max_tokens = 8;

SecureGapBuffer buffer_of(const char16_t* text) {
    SecureGapBuffer buffer;
    const char16_t* cursor = text;
    while (*cursor != u'\0') ++cursor;
    const std::size_t units = static_cast<std::size_t>(cursor - text);
    (void)buffer.assign(std::span<const char16_t>(text, units));
    return buffer;
}

struct ExpectedToken {
    std::size_t begin;
    std::size_t end;
    TokenKind kind;
};

void check_line(const char16_t* text, const bool is_ini,
                const std::initializer_list<ExpectedToken> expected) {
    SecureGapBuffer buffer = buffer_of(text);
    std::array<Token, max_tokens> tokens{};
    const std::size_t count = tokenize_line(buffer, 0, buffer.size(), is_ini,
                                            tokens.data(), tokens.size());
    const std::size_t expected_count = expected.size();
    CHECK(count == expected_count);
    std::size_t index = 0;
    for (const ExpectedToken& item : expected) {
        if (index >= count) break;
        CHECK(tokens[index].begin == item.begin);
        CHECK(tokens[index].end == item.end);
        CHECK(tokens[index].kind == item.kind);
        ++index;
    }
    // Every position must be covered by exactly one emitted token.
    std::size_t covered = 0;
    for (std::size_t i = 0; i < count; ++i) {
        CHECK(tokens[i].begin == covered);
        covered = tokens[i].end;
    }
    CHECK(covered == buffer.size());
}

void check_path(const wchar_t* path, const bool highlightable,
                const bool ini) {
    CHECK(mempad::editor::highlight::path_is_highlightable(path) == highlightable);
    CHECK(mempad::editor::highlight::path_is_ini(path) == ini);
}
} // namespace

void run_ini_env_highlight_tests() {
    // Empty and whitespace-only lines.
    check_line(u"", true, {});
    check_line(u"   ", false, {{0, 3, TokenKind::plain}});

    // Comments.
    check_line(u"; full comment", true, {{0, 14, TokenKind::comment}});
    check_line(u"  # padded", false, {{0, 2, TokenKind::plain},
                                      {2, 10, TokenKind::comment}});

    // .ini sections.
    check_line(u"[core]", true, {{0, 6, TokenKind::section}});
    check_line(u"  [db] ; note", true,
               {{0, 2, TokenKind::plain},
                {2, 6, TokenKind::section},
                {6, 7, TokenKind::plain},
                {7, 13, TokenKind::comment}});
    check_line(u"[unclosed", true, {{0, 9, TokenKind::plain}});

    // .env never treats '[' as a section.
    check_line(u"[core]", false, {{0, 6, TokenKind::plain}});

    // Keys and values, '=' and ':' separators.
    check_line(u"name=中文", true,
               {{0, 4, TokenKind::key},
                {4, 5, TokenKind::operator_},
                {5, 7, TokenKind::plain}});
    check_line(u"name : value", true,
               {{0, 4, TokenKind::key},
                {4, 5, TokenKind::plain},
                {5, 6, TokenKind::operator_},
                {6, 12, TokenKind::plain}});

    // ':' must not split .env values.
    check_line(u"URL=http://x", false,
               {{0, 3, TokenKind::key},
                {3, 4, TokenKind::operator_},
                {4, 12, TokenKind::plain}});

    // Quoted strings.
    check_line(u"KEY=\"hello world\"", false,
               {{0, 3, TokenKind::key},
                {3, 4, TokenKind::operator_},
                {4, 17, TokenKind::string_}});
    check_line(u"KEY= 'x'", false,
               {{0, 3, TokenKind::key},
                {3, 4, TokenKind::operator_},
                {4, 5, TokenKind::plain},
                {5, 8, TokenKind::string_}});
    check_line(u"KEY=\"unterminated", false,
               {{0, 3, TokenKind::key},
                {3, 4, TokenKind::operator_},
                {4, 17, TokenKind::string_}});

    // Bare numbers.
    check_line(u"PORT=8080", false,
               {{0, 4, TokenKind::key},
                {4, 5, TokenKind::operator_},
                {5, 9, TokenKind::number}});
    check_line(u"RATE = 1.5 # trailing", false,
               {{0, 4, TokenKind::key},
                {4, 5, TokenKind::plain},
                {5, 6, TokenKind::operator_},
                {6, 7, TokenKind::plain},
                {7, 10, TokenKind::number},
                {10, 11, TokenKind::plain},
                {11, 21, TokenKind::comment}});
    check_line(u"VER=1.5x", false,
               {{0, 3, TokenKind::key},
                {3, 4, TokenKind::operator_},
                {4, 8, TokenKind::plain}});

    // Trailing comments on plain values.
    check_line(u"A=1 ; note", true,
               {{0, 1, TokenKind::key},
                {1, 2, TokenKind::operator_},
                {2, 3, TokenKind::number},
                {3, 4, TokenKind::plain},
                {4, 10, TokenKind::comment}});

    // Quoted value hides the comment marker inside quotes.
    check_line(u"A=\"x # y\" ; note", true,
               {{0, 1, TokenKind::key},
                {1, 2, TokenKind::operator_},
                {2, 9, TokenKind::string_},
                {9, 10, TokenKind::plain},
                {10, 16, TokenKind::comment}});

    // A line without a separator is plain.
    check_line(u"just words", true, {{0, 10, TokenKind::plain}});
    check_line(u"KEYONLY", false, {{0, 7, TokenKind::plain}});

    // Empty key with a value.
    check_line(u"=value", true,
               {{0, 1, TokenKind::operator_},
                {1, 6, TokenKind::plain}});

    // Capacity limit: stops after the first two tokens.
    {
        SecureGapBuffer buffer = buffer_of(u"KEY=value");
        std::array<Token, 2> tokens{};
        const std::size_t count = tokenize_line(buffer, 0, buffer.size(), false,
                                                tokens.data(), tokens.size());
        CHECK(count == 2);
        CHECK(tokens[0].kind == TokenKind::key);
        CHECK(tokens[1].kind == TokenKind::operator_);
    }

    // Path extension checks, case-insensitive.
    check_path(L"settings.ini", true, true);
    check_path(L"SETTINGS.INI", true, true);
    check_path(L".env", true, false);
    check_path(L"config.env", true, false);
    check_path(L"readme.txt", false, false);
    check_path(L"ini", false, false);
    check_path(L"", false, false);
}
