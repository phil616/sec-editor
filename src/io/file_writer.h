#pragma once

#include "document/secure_document.h"
#include "encoding/encoding.h"
#include "io/io_error.h"

#include <string>

namespace mempad::io {

bool write_document(const std::wstring& path, const document::SecureDocument& document,
                    encoding::TextEncoding encoding, Error& error) noexcept;

} // namespace mempad::io
