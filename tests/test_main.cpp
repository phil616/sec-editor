#include "test_support.h"

#include "security/wer_guard.h"

int main() {
    CHECK(mempad::security::WerGuard::initialize());
    run_encoding_tests();
    run_clipboard_text_tests();
    run_gap_buffer_tests();
    run_document_tests();
    run_roundtrip_tests();
    std::printf("%d checks, %d failures\n", mempad::tests::checks, mempad::tests::failures);
    return mempad::tests::failures == 0 ? 0 : 1;
}
