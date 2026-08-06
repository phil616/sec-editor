#include "io/file_dialog.h"

#include <shobjidl.h>

namespace mempad::io {
namespace {
template <typename T>
class ComPtr final {
public:
    ~ComPtr() { if (pointer_ != nullptr) pointer_->Release(); }
    T** put() noexcept { return &pointer_; }
    T* get() const noexcept { return pointer_; }
    T* operator->() const noexcept { return pointer_; }
private:
    T* pointer_ = nullptr;
};

DialogResult extract_path(IFileDialog* dialog, std::wstring& path) noexcept {
    ComPtr<IShellItem> item;
    if (FAILED(dialog->GetResult(item.put()))) return DialogResult::failed;
    PWSTR raw = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &raw)) || raw == nullptr) {
        return DialogResult::failed;
    }
    path.assign(raw);
    CoTaskMemFree(raw);
    return DialogResult::selected;
}

DWORD encoding_item(const encoding::TextEncoding encoding) noexcept {
    switch (encoding) {
    case encoding::TextEncoding::ascii:
    case encoding::TextEncoding::utf8: return 1;
    case encoding::TextEncoding::utf8_bom: return 2;
    case encoding::TextEncoding::utf16_le: return 3;
    case encoding::TextEncoding::utf16_be: return 4;
    }
    return 1;
}
} // namespace

DialogResult choose_open_file(HWND owner, std::wstring& path) noexcept {
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_IFileOpenDialog,
                                reinterpret_cast<void**>(dialog.put())))) {
        return DialogResult::failed;
    }
    FILEOPENDIALOGOPTIONS options{};
    if (FAILED(dialog->GetOptions(&options)) ||
        FAILED(dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST |
                                  FOS_PATHMUSTEXIST | FOS_DONTADDTORECENT))) {
        return DialogResult::failed;
    }
    const COMDLG_FILTERSPEC filters[]{{L"Text files", L"*.txt;*.conf;*.cfg;*.ini;*.env;*.key"},
                                      {L"All files", L"*.*"}};
    (void)dialog->SetFileTypes(2, filters);
    const HRESULT shown = dialog->Show(owner);
    if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return DialogResult::cancelled;
    if (FAILED(shown)) return DialogResult::failed;
    return extract_path(dialog.get(), path);
}

DialogResult choose_save_file(HWND owner, const std::wstring& current_path,
                              const encoding::TextEncoding current_encoding,
                              std::wstring& path,
                              encoding::TextEncoding& selected_encoding) noexcept {
    ComPtr<IFileSaveDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_IFileSaveDialog,
                                reinterpret_cast<void**>(dialog.put())))) {
        return DialogResult::failed;
    }
    FILEOPENDIALOGOPTIONS options{};
    if (FAILED(dialog->GetOptions(&options)) ||
        FAILED(dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST |
                                  FOS_OVERWRITEPROMPT | FOS_DONTADDTORECENT))) {
        return DialogResult::failed;
    }
    const COMDLG_FILTERSPEC filters[]{{L"Text files", L"*.txt"}, {L"All files", L"*.*"}};
    (void)dialog->SetFileTypes(2, filters);
    if (!current_path.empty()) {
        const std::size_t slash = current_path.find_last_of(L"\\/");
        (void)dialog->SetFileName(current_path.c_str() +
            (slash == std::wstring::npos ? 0U : slash + 1U));
    } else {
        (void)dialog->SetFileName(L"document.txt");
    }
    ComPtr<IFileDialogCustomize> customize;
    constexpr DWORD combo = 100;
    if (FAILED(dialog->QueryInterface(IID_IFileDialogCustomize,
                                     reinterpret_cast<void**>(customize.put()))) ||
        FAILED(customize->AddComboBox(combo)) ||
        FAILED(customize->SetControlLabel(combo, L"Encoding"))) {
        return DialogResult::failed;
    }
    (void)customize->AddControlItem(combo, 1, L"UTF-8");
    (void)customize->AddControlItem(combo, 2, L"UTF-8 with BOM");
    (void)customize->AddControlItem(combo, 3, L"UTF-16 LE");
    (void)customize->AddControlItem(combo, 4, L"UTF-16 BE");
    (void)customize->SetSelectedControlItem(combo, encoding_item(current_encoding));
    const HRESULT shown = dialog->Show(owner);
    if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return DialogResult::cancelled;
    if (FAILED(shown)) return DialogResult::failed;
    DWORD selected = 1;
    (void)customize->GetSelectedControlItem(combo, &selected);
    switch (selected) {
    case 2: selected_encoding = encoding::TextEncoding::utf8_bom; break;
    case 3: selected_encoding = encoding::TextEncoding::utf16_le; break;
    case 4: selected_encoding = encoding::TextEncoding::utf16_be; break;
    default: selected_encoding = encoding::TextEncoding::utf8; break;
    }
    return extract_path(dialog.get(), path);
}

} // namespace mempad::io
