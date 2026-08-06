#include "test_support.h"

#include "document/secure_gap_buffer.h"

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace {
std::u16string snapshot(const mempad::document::SecureGapBuffer& buffer) {
    std::u16string result(buffer.size(), u'\0');
    CHECK(buffer.copy_to(result));
    return result;
}
}

void run_gap_buffer_tests() {
    using mempad::document::SecureGapBuffer;
    SecureGapBuffer buffer;
    CHECK(buffer.assign(std::u16string_view{u"alpha"}));
    CHECK(buffer.insert(5, std::u16string_view{u" beta"}));
    CHECK(snapshot(buffer) == u"alpha beta");
    CHECK(buffer.insert(0, std::u16string_view{u"["}));
    CHECK(buffer.insert(buffer.size(), std::u16string_view{u"]"}));
    CHECK(snapshot(buffer) == u"[alpha beta]");
    CHECK(buffer.erase(1, 6));
    CHECK(snapshot(buffer) == u"[ beta]");
    CHECK(buffer.replace(1, 2, std::u16string_view{u"value"}));
    CHECK(snapshot(buffer) == u"[valuebeta]");

    std::u16string large(12000, u'x');
    CHECK(buffer.assign(large));
    CHECK(buffer.capacity() >= large.size());
    CHECK(snapshot(buffer) == large);

    const std::array<char16_t, 3> scalar{0xD83D, 0xDE00, u'x'};
    CHECK(buffer.assign(scalar));
    CHECK(buffer.next_scalar(0) == 2);
    CHECK(buffer.previous_scalar(2) == 0);
    CHECK(buffer.next_scalar(2) == 3);
    CHECK(buffer.previous_scalar(3) == 2);
    CHECK(buffer.erase(buffer.previous_scalar(2), 2));
    CHECK(snapshot(buffer) == u"x");

    CHECK(buffer.assign(scalar));
    CHECK(buffer.erase(0, buffer.next_scalar(0)));
    CHECK(snapshot(buffer) == u"x");
}
