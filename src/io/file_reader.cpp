#include "io/file_reader.h"

#include "encoding/encoding.h"
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

void fail(Error& error, const ErrorCode code, const DWORD system = 0,
          const std::size_t offset = 0) noexcept {
    error = {code, system, offset};
}
} // namespace

const wchar_t* error_name(const ErrorCode code) noexcept {
    switch (code) {
    case ErrorCode::none: return L"No error";
    case ErrorCode::open_failed: return L"Unable to open file";
    case ErrorCode::not_regular_file: return L"The selected path is not a regular file";
    case ErrorCode::file_too_large: return L"The file exceeds the 8 MiB input limit";
    case ErrorCode::size_changed: return L"The file changed size while it was being read";
    case ErrorCode::read_failed: return L"The file could not be read completely";
    case ErrorCode::invalid_encoding: return L"Unsupported or invalid text encoding";
    case ErrorCode::document_too_large: return L"The decoded document exceeds a safety limit";
    case ErrorCode::secure_memory_failed: return L"Secure memory initialization failed";
    case ErrorCode::encode_failed: return L"The document could not be encoded";
    case ErrorCode::write_failed: return L"The target file could not be written";
    case ErrorCode::flush_failed: return L"The saved file could not be flushed to disk";
    }
    return L"Unknown error";
}

bool read_document(const std::wstring& path, document::SecureDocument& document,
                   Error& error) noexcept {
    error = {};
    Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (file.get() == INVALID_HANDLE_VALUE) {
        fail(error, ErrorCode::open_failed, GetLastError());
        return false;
    }
    if (GetFileType(file.get()) != FILE_TYPE_DISK) {
        fail(error, ErrorCode::not_regular_file);
        return false;
    }
    BY_HANDLE_FILE_INFORMATION info{};
    if (GetFileInformationByHandle(file.get(), &info) == 0 ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        fail(error, ErrorCode::not_regular_file, GetLastError());
        return false;
    }
    LARGE_INTEGER initial_size{};
    if (GetFileSizeEx(file.get(), &initial_size) == 0 || initial_size.QuadPart < 0) {
        fail(error, ErrorCode::read_failed, GetLastError());
        return false;
    }
    if (static_cast<unsigned long long>(initial_size.QuadPart) > max_input_file) {
        fail(error, ErrorCode::file_too_large);
        return false;
    }
    const std::size_t byte_count = static_cast<std::size_t>(initial_size.QuadPart);
    security::SecureAllocation raw;
    if (!raw.allocate(std::max<std::size_t>(byte_count, 1U))) {
        fail(error, ErrorCode::secure_memory_failed, GetLastError());
        return false;
    }
    std::size_t total = 0;
    while (total < byte_count) {
        const std::size_t remaining = byte_count - total;
        const DWORD request = static_cast<DWORD>(std::min<std::size_t>(remaining, 1U << 20U));
        DWORD received = 0;
        if (ReadFile(file.get(), raw.data() + total, request, &received, nullptr) == 0 ||
            received == 0) {
            fail(error, ErrorCode::read_failed, GetLastError());
            return false;
        }
        total += received;
    }
    LARGE_INTEGER final_size{};
    if (GetFileSizeEx(file.get(), &final_size) == 0 ||
        final_size.QuadPart != initial_size.QuadPart) {
        fail(error, ErrorCode::size_changed, GetLastError());
        return false;
    }
    const std::span<const std::byte> input(raw.data(), byte_count);
    const encoding::Analysis analysis = encoding::analyze(input);
    if (!analysis.ok) {
        fail(error, ErrorCode::invalid_encoding, 0, analysis.error_offset);
        return false;
    }
    if (analysis.utf16_units > document::SecureGapBuffer::max_units) {
        fail(error, ErrorCode::document_too_large);
        return false;
    }
    security::SecureAllocation decoded;
    const std::size_t decoded_bytes = analysis.utf16_units * sizeof(char16_t);
    if (!decoded.allocate(std::max<std::size_t>(decoded_bytes, sizeof(char16_t)))) {
        fail(error, ErrorCode::secure_memory_failed, GetLastError());
        return false;
    }
    std::span<char16_t> text(reinterpret_cast<char16_t*>(decoded.data()),
                             analysis.utf16_units);
    if (!encoding::decode(input, analysis, text)) {
        fail(error, ErrorCode::invalid_encoding, 0, analysis.error_offset);
        return false;
    }
    if (!document.load(text, analysis.encoding, analysis.newline, path)) {
        fail(error, analysis.utf16_units > document::SecureGapBuffer::max_units
                        ? ErrorCode::document_too_large
                        : ErrorCode::secure_memory_failed,
             GetLastError());
        return false;
    }
    return true;
}

} // namespace mempad::io
