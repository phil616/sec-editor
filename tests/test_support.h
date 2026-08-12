#pragma once

#include <cstdio>

namespace mempad::tests {
inline int failures = 0;
inline int checks = 0;

inline void check(const bool condition, const char* expression,
                  const char* file, const int line) {
    ++checks;
    if (!condition) {
        ++failures;
        std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", file, line, expression);
    }
}
} // namespace mempad::tests

#define CHECK(expression) ::mempad::tests::check((expression), #expression, __FILE__, __LINE__)

void run_encoding_tests();
void run_clipboard_text_tests();
void run_gap_buffer_tests();
void run_document_tests();
void run_roundtrip_tests();
