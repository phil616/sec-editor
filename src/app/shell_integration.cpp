#include "app/shell_integration.h"

#include <array>
#include <string>
#include <string_view>
#include <vector>
#include <shlobj.h>

namespace mempad::app::shell_integration {
namespace {
constexpr std::array supported_extensions{
    L".txt", L".text", L".md", L".csv", L".pem", L".env", L".ini",
    L".yaml", L".yml", L".key", L".json", L".crt", L".cer", L".sig",
    L".csr", L".req", L".conf", L".inf",
};
constexpr wchar_t registry_prefix[] =
    L"Software\\Classes\\SystemFileAssociations\\";
constexpr wchar_t verb_suffix[] = L"\\shell\\Mempad.Open";

class RegistryKey final {
public:
    RegistryKey() noexcept = default;
    ~RegistryKey() { if (key_ != nullptr) RegCloseKey(key_); }
    RegistryKey(const RegistryKey&) = delete;
    RegistryKey& operator=(const RegistryKey&) = delete;

    HKEY* receive() noexcept { return &key_; }
    HKEY get() const noexcept { return key_; }

private:
    HKEY key_ = nullptr;
};

std::wstring verb_key_path(const std::wstring_view extension) {
    std::wstring path(registry_prefix);
    path.append(extension);
    path.append(verb_suffix);
    return path;
}

LSTATUS set_string(const HKEY key, const wchar_t* name,
                   const std::wstring_view value) noexcept {
    const auto bytes = static_cast<DWORD>((value.size() + 1U) * sizeof(wchar_t));
    return RegSetValueExW(key, name, 0, REG_SZ,
        reinterpret_cast<const BYTE*>(value.data()), bytes);
}

LSTATUS current_executable_path(std::wstring& path) noexcept {
    // The documented maximum NT path length, including the terminator.
    std::vector<wchar_t> buffer(32768U, L'\0');
    SetLastError(ERROR_SUCCESS);
    const DWORD length = GetModuleFileNameW(
        nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0) return static_cast<LSTATUS>(GetLastError());
    if (length >= buffer.size()) return ERROR_INSUFFICIENT_BUFFER;
    path.assign(buffer.data(), length);
    return ERROR_SUCCESS;
}

LSTATUS install_extension(const wchar_t* extension,
                          const std::wstring& executable) noexcept {
    const std::wstring path = verb_key_path(extension);
    RegistryKey verb;
    LSTATUS status = RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr,
                                    REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr,
                                    verb.receive(), nullptr);
    if (status != ERROR_SUCCESS) return status;

    constexpr std::wstring_view label = L"使用 mempad 打开";
    constexpr std::wstring_view selection_model = L"Single";
    status = set_string(verb.get(), nullptr, label);
    if (status == ERROR_SUCCESS) status = set_string(verb.get(), L"MUIVerb", label);
    if (status == ERROR_SUCCESS) {
        status = set_string(verb.get(), L"MultiSelectModel", selection_model);
    }

    std::wstring icon;
    if (status == ERROR_SUCCESS) {
        icon.reserve(executable.size() + 4U);
        icon.push_back(L'\"');
        icon.append(executable);
        icon.append(L"\",0");
        status = set_string(verb.get(), L"Icon", icon);
    }
    if (status != ERROR_SUCCESS) return status;

    RegistryKey command_key;
    status = RegCreateKeyExW(verb.get(), L"command", 0, nullptr,
                             REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr,
                             command_key.receive(), nullptr);
    if (status != ERROR_SUCCESS) return status;

    std::wstring command;
    command.reserve(executable.size() + 9U);
    command.push_back(L'\"');
    command.append(executable);
    command.append(L"\" \"%1\"");
    return set_string(command_key.get(), nullptr, command);
}

void notify_shell() noexcept {
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}
} // namespace

Result install() noexcept {
    std::wstring executable;
    const LSTATUS executable_status = current_executable_path(executable);
    if (executable_status != ERROR_SUCCESS) {
        return {false, static_cast<DWORD>(executable_status), nullptr};
    }

    DWORD first_error = ERROR_SUCCESS;
    const wchar_t* failed_extension = nullptr;
    for (const wchar_t* extension : supported_extensions) {
        const LSTATUS status = install_extension(extension, executable);
        if (status != ERROR_SUCCESS && first_error == ERROR_SUCCESS) {
            first_error = static_cast<DWORD>(status);
            failed_extension = extension;
        }
    }
    notify_shell();
    return {first_error == ERROR_SUCCESS, first_error, failed_extension};
}

Result uninstall() noexcept {
    DWORD first_error = ERROR_SUCCESS;
    const wchar_t* failed_extension = nullptr;
    for (const wchar_t* extension : supported_extensions) {
        const std::wstring path = verb_key_path(extension);
        const LSTATUS status = RegDeleteTreeW(HKEY_CURRENT_USER, path.c_str());
        if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND &&
            status != ERROR_PATH_NOT_FOUND && first_error == ERROR_SUCCESS) {
            first_error = static_cast<DWORD>(status);
            failed_extension = extension;
        }
    }
    notify_shell();
    return {first_error == ERROR_SUCCESS, first_error, failed_extension};
}

} // namespace mempad::app::shell_integration
