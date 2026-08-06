#pragma once

#include "document/secure_document.h"
#include "io/io_error.h"

#include <string>

namespace mempad::io {

constexpr std::size_t max_input_file = 8U * 1024U * 1024U;

bool read_document(const std::wstring& path, document::SecureDocument& document,
                   Error& error) noexcept;

} // namespace mempad::io
