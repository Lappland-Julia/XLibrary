#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <winver.h>
#endif

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <iomanip>
#include <iterator>
#include <memory>
#include <ostream>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include "Application.h"
#ifndef _WIN32
#include "linux_process.h"
#endif

std::uint32_t Application::get_pid() const noexcept {
#ifdef _WIN32
    if (!this->process_handle || WaitForSingleObject(this->process_handle.get(), 0) != WAIT_TIMEOUT) {
        return 0;
    }
    return this->pid;
#else
    if (!this->process_handle) {
        return 0;
    }
    try {
        std::error_code error;
        const auto identity = std::static_pointer_cast<xlibrary::process_identity>(this->process_handle);
        return xlibrary::is_process_alive(*identity, error) ? this->pid : 0;
    } catch (...) {
        return 0;
    }
#endif
}

std::string Application::read_application_name(const std::filesystem::path &executable_path) {
    const auto filename_utf8 = executable_path.stem().u8string();
    const std::string fallback_name(filename_utf8.begin(), filename_utf8.end());

#ifdef _WIN32
    DWORD ignored_handle{};
    const DWORD version_size = GetFileVersionInfoSizeW(executable_path.c_str(), &ignored_handle);
    if (version_size == 0) {
        return fallback_name;
    }

    std::vector<std::byte> version_info(version_size);
    if (!GetFileVersionInfoW(executable_path.c_str(), 0, version_size, version_info.data())) {
        return fallback_name;
    }

    struct language_and_code_page {
        WORD language;
        WORD code_page;
    };

    void *translation_data{};
    UINT translation_size{};
    if (!VerQueryValueW(version_info.data(), L"\\VarFileInfo\\Translation", &translation_data, &translation_size) ||
        translation_data == nullptr || translation_size == 0 ||
        translation_size % sizeof(language_and_code_page) != 0) {
        return fallback_name;
    }

    const auto translation_count = translation_size / sizeof(language_and_code_page);
    const wchar_t *field_names[] = {L"FileDescription", L"ProductName"};

    // Look for a description in every language before trying the product name.
    for (const auto *field_name : field_names) {
        for (std::size_t index = 0; index < translation_count; ++index) {
            language_and_code_page translation{};
            std::memcpy(&translation, static_cast<const std::byte *>(translation_data) + index * sizeof(translation),
                        sizeof(translation));

            wchar_t query[64]{};
            const int query_length = std::swprintf(query, std::size(query), L"\\StringFileInfo\\%04x%04x\\%ls",
                                                   static_cast<unsigned int>(translation.language),
                                                   static_cast<unsigned int>(translation.code_page), field_name);
            if (query_length <= 0) {
                continue;
            }

            void *name_data{};
            UINT name_length{};
            if (!VerQueryValueW(version_info.data(), query, &name_data, &name_length) || name_data == nullptr ||
                name_length == 0) {
                continue;
            }

            // The string belongs to version_info; copy it before releasing the buffer.
            std::wstring_view name(static_cast<const wchar_t *>(name_data), name_length);
            name = name.substr(0, name.find(L'\0'));
            if (name.empty() || !std::in_range<int>(name.size())) {
                continue;
            }

            const int character_count = static_cast<int>(name.size());
            const int utf8_size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, name.data(), character_count,
                                                      nullptr, 0, nullptr, nullptr);
            if (utf8_size == 0) {
                continue;
            }

            std::string application_name(static_cast<std::size_t>(utf8_size), '\0');
            if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, name.data(), character_count,
                                    application_name.data(), utf8_size, nullptr, nullptr) == utf8_size) {
                return application_name;
            }
        }
    }
#endif

    return fallback_name;
}

std::uint32_t Application::launch() {
    return this->launch({}, this->file_path.parent_path());
}

std::uint32_t Application::launch(const std::wstring &arguments, const std::filesystem::path &working_directory) {
    const std::filesystem::path executable_path = this->get_file_path();
    if (executable_path.empty()) {
        throw std::invalid_argument("Application executable path is empty");
    }

#ifdef _WIN32
    SHELLEXECUTEINFOW launch_info{};
    launch_info.cbSize = sizeof(launch_info);
    launch_info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    launch_info.lpVerb = L"open";
    launch_info.lpFile = executable_path.c_str();
    launch_info.lpParameters = arguments.empty() ? nullptr : arguments.c_str();
    launch_info.lpDirectory = working_directory.c_str();
    launch_info.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&launch_info)) {
        const auto error_code = GetLastError();
        throw std::runtime_error("Application launch failed: " + std::to_string(error_code));
    }

    std::unique_ptr<void, decltype(&CloseHandle)> process_handle(launch_info.hProcess, &CloseHandle);
    if (!process_handle) {
        // Shell may hand off the request to an existing application without creating a process.
        return 0;
    }

    const auto process_id = GetProcessId(process_handle.get());
    if (process_id == 0) {
        return 0;
    }

    this->process_handle = std::move(process_handle);
    this->pid = process_id;
    return process_id;
#else
    auto identity = std::make_shared<xlibrary::process_identity>();
    *identity = xlibrary::launch_process(executable_path, arguments, working_directory);
    this->pid = identity->pid;
    this->process_handle = std::move(identity);
    return this->pid;
#endif
}

std::ostream &operator<<(std::ostream &stream, const Application &application) {
    const auto path_utf8 = application.get_file_path().u8string();
    const std::string path_string(path_utf8.begin(), path_utf8.end());

    return stream << "Application object: {\n"
                  << "\tPID: " << application.get_pid() << "\n"
                  << "\tTitle: " << application.get_file_title() << "\n"
                  << "\tPath: " << std::quoted(path_string) << " }";
}

void Application::set_executable(std::filesystem::path const &file_path) {
    if (file_path.empty()) {
        throw std::invalid_argument("Application executable path is empty");
    }
    std::filesystem::path executable_path = std::filesystem::absolute(file_path);

#ifdef _WIN32
    auto extension = executable_path.extension().wstring();
    std::ranges::transform(extension, extension.begin(),
                           [](wchar_t character) { return static_cast<wchar_t>(std::towlower(character)); });
#endif

    const auto name_utf8 = executable_path.stem().u8string();
    std::string application_name(name_utf8.begin(), name_utf8.end());

#ifdef _WIN32
    if (extension == L"") {
        throw std::invalid_argument("Invalid app path");
    }

    if (extension == L".exe" && std::filesystem::is_regular_file(executable_path)) {
        application_name = this->read_application_name(executable_path);
    }
#endif

    if (this->file_path != executable_path) {
        this->process_handle.reset();
        this->pid = 0;
    }
    this->file_path = executable_path;
    this->file_title = application_name;
}

Application::Application() {
    this->set_empty();
}

Application::Application(const std::string &file_title, const std::filesystem::path &executable_path) : Application() {
    if (!executable_path.empty()) {
        this->set_executable(executable_path);
    }
    this->file_title = file_title;
}

Application::Application(const std::filesystem::path &executable_path) {
    this->set_executable(executable_path);
}
