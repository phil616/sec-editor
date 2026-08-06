#pragma once

#include "encoding/encoding.h"

#include <string>
#include <windows.h>

namespace mempad::io {

enum class DialogResult { selected, cancelled, failed };

DialogResult choose_open_file(HWND owner, std::wstring& path) noexcept;
DialogResult choose_save_file(HWND owner, const std::wstring& current_path,
                              encoding::TextEncoding current_encoding,
                              std::wstring& path,
                              encoding::TextEncoding& selected_encoding) noexcept;

} // namespace mempad::io
