#include "io/file_writer.h"

#include "security/secure_allocation.h"

#include <algorithm>
#include <cstddef>
#include <span>
#include <windows.h>

namespace mempad::io {
namespace {
class Handle final {
public:
    explicit Handle(HANDLE value) noexcept : value_(value) {}
    ~Handle() { if (value_ != INVALID_HANDLE_VALUE) CloseHandle(value_); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE get() const noexcept { return value_; }
private:
    HANDLE value_;
};
}

bool write_document(const std::wstring& path,
                    const document::SecureDocument& document,
                    const encoding::TextEncoding selected_encoding,
                    Error& error) noexcept {
    error = {};
    if (!document.open()) {
        error.code = ErrorCode::encode_failed;
        return false;
    }
    const std::size_t units = document.text().size();
    security::SecureAllocation contiguous;
    if (!contiguous.allocate(std::max<std::size_t>(units * sizeof(char16_t),
                                                   sizeof(char16_t)))) {
        error = {ErrorCode::secure_memory_failed, GetLastError(), 0};
        return false;
    }
    std::span<char16_t> text(reinterpret_cast<char16_t*>(contiguous.data()), units);
    if (!document.text().copy_to(text)) {
        error.code = ErrorCode::encode_failed;
        return false;
    }
    std::size_t output_size = 0;
    if (!encoding::encoded_size(text, selected_encoding,
                                document.newline().selected, output_size)) {
        error.code = ErrorCode::encode_failed;
        return false;
    }
    security::SecureAllocation encoded;
    if (!encoded.allocate(std::max<std::size_t>(output_size, 1U))) {
        error = {ErrorCode::secure_memory_failed, GetLastError(), 0};
        return false;
    }
    std::size_t written_size = 0;
    if (!encoding::encode(text, selected_encoding, document.newline().selected,
                          std::span<std::byte>(encoded.data(), output_size), written_size) ||
        written_size != output_size) {
        error.code = ErrorCode::encode_failed;
        return false;
    }
    Handle file(CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr));
    if (file.get() == INVALID_HANDLE_VALUE) {
        error = {ErrorCode::open_failed, GetLastError(), 0};
        return false;
    }
    std::size_t total = 0;
    while (total < output_size) {
        const DWORD request = static_cast<DWORD>(std::min<std::size_t>(
            output_size - total, 1U << 20U));
        DWORD written = 0;
        if (WriteFile(file.get(), encoded.data() + total, request, &written, nullptr) == 0 ||
            written == 0) {
            error = {ErrorCode::write_failed, GetLastError(), total};
            return false;
        }
        total += written;
    }
    if (FlushFileBuffers(file.get()) == 0) {
        error = {ErrorCode::flush_failed, GetLastError(), total};
        return false;
    }
    return true;
}

} // namespace mempad::io
