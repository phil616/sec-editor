#pragma once

#include <cstdint>

namespace mempad::io {

enum class ErrorCode {
    none,
    open_failed,
    not_regular_file,
    file_too_large,
    size_changed,
    read_failed,
    invalid_encoding,
    document_too_large,
    secure_memory_failed,
    encode_failed,
    write_failed,
    flush_failed
};

struct Error final {
    ErrorCode code = ErrorCode::none;
    std::uint32_t system_code = 0;
    std::size_t offset = 0;
};

const wchar_t* error_name(ErrorCode code) noexcept;

} // namespace mempad::io
