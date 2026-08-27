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
constexpr wchar_t classes_extension_prefix[] = L"Software\\Classes\\";
constexpr wchar_t explorer_extension_prefix[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\";
constexpr wchar_t open_with_suffix[] = L"\\OpenWithProgids";
constexpr wchar_t application_key_path[] =
    L"Software\\Classes\\Applications\\mempad.exe";
constexpr wchar_t progid[] = L"Mempad.Text.1";
constexpr wchar_t progid_key_path[] = L"Software\\Classes\\Mempad.Text.1";

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

std::wstring open_with_key_path(const std::wstring_view prefix,
                                const std::wstring_view extension) {
    std::wstring path(prefix);
    path.append(extension);
    path.append(open_with_suffix);
    return path;
}

LSTATUS set_string(const HKEY key, const wchar_t* name,
                   const std::wstring_view value) noexcept {
    const auto bytes = static_cast<DWORD>((value.size() + 1U) * sizeof(wchar_t));
    return RegSetValueExW(key, name, 0, REG_SZ,
        reinterpret_cast<const BYTE*>(value.data()), bytes);
}

LSTATUS create_key(const HKEY parent, const wchar_t* path,
                   RegistryKey& key) noexcept {
    return RegCreateKeyExW(parent, path, 0, nullptr, REG_OPTION_NON_VOLATILE,
                           KEY_WRITE, nullptr, key.receive(), nullptr);
}

std::wstring icon_for(const std::wstring& executable) {
    std::wstring icon;
    icon.reserve(executable.size() + 4U);
    icon.push_back(L'\"');
    icon.append(executable);
    icon.append(L"\",0");
    return icon;
}

std::wstring command_for(const std::wstring& executable) {
    std::wstring command;
    command.reserve(executable.size() + 9U);
    command.push_back(L'\"');
    command.append(executable);
    command.append(L"\" \"%1\"");
    return command;
}

LSTATUS install_application_registration(
    const std::wstring& executable) noexcept {
    const std::wstring icon = icon_for(executable);
    const std::wstring command = command_for(executable);

    RegistryKey application;
    LSTATUS status = create_key(HKEY_CURRENT_USER, application_key_path,
                                application);
    if (status != ERROR_SUCCESS) return status;
    status = set_string(application.get(), L"FriendlyAppName", L"mempad");
    if (status != ERROR_SUCCESS) return status;

    RegistryKey application_icon;
    status = create_key(application.get(), L"DefaultIcon", application_icon);
    if (status == ERROR_SUCCESS) {
        status = set_string(application_icon.get(), nullptr, icon);
    }
    if (status != ERROR_SUCCESS) return status;

    RegistryKey supported_types;
    status = create_key(application.get(), L"SupportedTypes", supported_types);
    if (status != ERROR_SUCCESS) return status;
    for (const wchar_t* extension : supported_extensions) {
        status = set_string(supported_types.get(), extension, L"");
        if (status != ERROR_SUCCESS) return status;
    }

    RegistryKey application_command;
    status = create_key(application.get(), L"shell\\open\\command",
                        application_command);
    if (status == ERROR_SUCCESS) {
        status = set_string(application_command.get(), nullptr, command);
    }
    if (status != ERROR_SUCCESS) return status;

    RegistryKey progid_key;
    status = create_key(HKEY_CURRENT_USER, progid_key_path, progid_key);
    if (status != ERROR_SUCCESS) return status;
    status = set_string(progid_key.get(), nullptr, L"mempad text document");
    if (status != ERROR_SUCCESS) return status;

    RegistryKey progid_application;
    status = create_key(progid_key.get(), L"Application", progid_application);
    if (status == ERROR_SUCCESS) {
        status = set_string(progid_application.get(), L"ApplicationName", L"mempad");
    }
    if (status == ERROR_SUCCESS) {
        status = set_string(progid_application.get(), L"ApplicationIcon", icon);
    }
    if (status == ERROR_SUCCESS) {
        status = set_string(progid_application.get(), L"ApplicationCompany",
                            L"GreenShadeCapitalSecTeam");
    }
    if (status != ERROR_SUCCESS) return status;

    RegistryKey progid_icon;
    status = create_key(progid_key.get(), L"DefaultIcon", progid_icon);
    if (status == ERROR_SUCCESS) {
        status = set_string(progid_icon.get(), nullptr, icon);
    }
    if (status != ERROR_SUCCESS) return status;

    RegistryKey progid_command;
    status = create_key(progid_key.get(), L"shell\\open\\command", progid_command);
    if (status == ERROR_SUCCESS) {
        status = set_string(progid_command.get(), nullptr, command);
    }
    return status;
}

LSTATUS install_open_with_extension(const wchar_t* extension) noexcept {
    const std::array prefixes{
        std::wstring_view(classes_extension_prefix),
        std::wstring_view(explorer_extension_prefix),
    };
    for (const std::wstring_view prefix : prefixes) {
        const std::wstring path = open_with_key_path(prefix, extension);
        RegistryKey key;
        LSTATUS status = create_key(HKEY_CURRENT_USER, path.c_str(), key);
        if (status != ERROR_SUCCESS) return status;
        status = set_string(key.get(), progid, L"");
        if (status != ERROR_SUCCESS) return status;
    }
    return ERROR_SUCCESS;
}

LSTATUS delete_open_with_extension(const wchar_t* extension) noexcept {
    LSTATUS first_error = ERROR_SUCCESS;
    const std::array prefixes{
        std::wstring_view(classes_extension_prefix),
        std::wstring_view(explorer_extension_prefix),
    };
    for (const std::wstring_view prefix : prefixes) {
        const std::wstring path = open_with_key_path(prefix, extension);
        RegistryKey key;
        LSTATUS status = RegOpenKeyExW(HKEY_CURRENT_USER, path.c_str(), 0,
                                      KEY_SET_VALUE, key.receive());
        if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND) continue;
        if (status == ERROR_SUCCESS) status = RegDeleteValueW(key.get(), progid);
        if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND &&
            first_error == ERROR_SUCCESS) {
            first_error = status;
        }
    }
    return first_error;
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
        icon = icon_for(executable);
        status = set_string(verb.get(), L"Icon", icon);
    }
    if (status != ERROR_SUCCESS) return status;

    RegistryKey command_key;
    status = RegCreateKeyExW(verb.get(), L"command", 0, nullptr,
                             REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr,
                             command_key.receive(), nullptr);
    if (status != ERROR_SUCCESS) return status;

    const std::wstring command = command_for(executable);
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
    const LSTATUS application_status = install_application_registration(executable);
    if (application_status != ERROR_SUCCESS) {
        first_error = static_cast<DWORD>(application_status);
    }
    for (const wchar_t* extension : supported_extensions) {
        const LSTATUS verb_status = install_extension(extension, executable);
        const LSTATUS open_with_status = install_open_with_extension(extension);
        const LSTATUS status = verb_status != ERROR_SUCCESS
            ? verb_status : open_with_status;
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
        const LSTATUS verb_status = RegDeleteTreeW(HKEY_CURRENT_USER, path.c_str());
        const LSTATUS open_with_status = delete_open_with_extension(extension);
        const LSTATUS status = verb_status != ERROR_SUCCESS &&
                               verb_status != ERROR_FILE_NOT_FOUND &&
                               verb_status != ERROR_PATH_NOT_FOUND
            ? verb_status : open_with_status;
        if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND &&
            status != ERROR_PATH_NOT_FOUND && first_error == ERROR_SUCCESS) {
            first_error = static_cast<DWORD>(status);
            failed_extension = extension;
        }
    }
    const std::array owned_keys{application_key_path, progid_key_path};
    for (const wchar_t* path : owned_keys) {
        const LSTATUS status = RegDeleteTreeW(HKEY_CURRENT_USER, path);
        if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND &&
            status != ERROR_PATH_NOT_FOUND && first_error == ERROR_SUCCESS) {
            first_error = static_cast<DWORD>(status);
        }
    }
    notify_shell();
    return {first_error == ERROR_SUCCESS, first_error, failed_extension};
}

} // namespace mempad::app::shell_integration
