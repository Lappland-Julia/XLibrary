#include "Application.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <objbase.h>
#include <ostream>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>
#include <winver.h>

std::string Application::generate_uuid()
{
    GUID uuid{};

    if (const HRESULT result = CoCreateGuid(&uuid); FAILED(result)) {
        throw std::runtime_error("UUID creation failed");
    }

    wchar_t buffer[39]{};
    const int length = StringFromGUID2(uuid, buffer, 39);

    if (length == 0) {
        throw std::runtime_error("UUID conversion failed");
    }

    std::string uuid_string;
    uuid_string.reserve(36);

    // Пропускаем скобки и завершающий ноль; символы GUID входят в ASCII.
    for (int i = 1; i < length - 2; ++i) {
        uuid_string.push_back(static_cast<char>(buffer[i]));
    }

    return uuid_string;
}

std::string Application::read_application_name(const std::filesystem::path &executable_path) {
    const auto filename_utf8 = executable_path.stem().u8string();
    const std::string fallback_name(filename_utf8.begin(), filename_utf8.end());

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
    if (!VerQueryValueW(version_info.data(), L"\\VarFileInfo\\Translation",
                        &translation_data, &translation_size) ||
        translation_data == nullptr || translation_size == 0 ||
        translation_size % sizeof(language_and_code_page) != 0) {
        return fallback_name;
    }

    const auto translation_count = translation_size / sizeof(language_and_code_page);
    const wchar_t *field_names[] = {L"FileDescription", L"ProductName"};

    // Сначала ищем описание во всех языках, затем пробуем название продукта.
    for (const auto *field_name : field_names) {
        for (std::size_t index = 0; index < translation_count; ++index) {
            language_and_code_page translation{};
            std::memcpy(&translation,
                        static_cast<const std::byte *>(translation_data) + index * sizeof(translation),
                        sizeof(translation));

            wchar_t query[64]{};
            const int query_length = std::swprintf(
                query, std::size(query), L"\\StringFileInfo\\%04x%04x\\%ls",
                static_cast<unsigned int>(translation.language),
                static_cast<unsigned int>(translation.code_page), field_name);
            if (query_length <= 0) {
                continue;
            }

            void *name_data{};
            UINT name_length{};
            if (!VerQueryValueW(version_info.data(), query, &name_data, &name_length) ||
                name_data == nullptr || name_length == 0) {
                continue;
            }

            // Строка принадлежит version_info; копируем её до освобождения буфера.
            std::wstring_view name(static_cast<const wchar_t *>(name_data), name_length);
            name = name.substr(0, name.find(L'\0'));
            if (name.empty() || !std::in_range<int>(name.size())) {
                continue;
            }

            const int character_count = static_cast<int>(name.size());
            const int utf8_size = WideCharToMultiByte(
                CP_UTF8, WC_ERR_INVALID_CHARS, name.data(), character_count,
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

    return fallback_name;
}

void Application::launch() const {
    const std::filesystem::path executable_path = this->get_file_path();
    const auto working_directory = executable_path.parent_path();

    const auto result = ShellExecuteW(
        nullptr,
        L"open",
        executable_path.c_str(),
        nullptr,                   // Аргументы запуска.
        working_directory.c_str(),
        SW_SHOWNORMAL
    );

    if (const auto error_code = reinterpret_cast<INT_PTR>(result); error_code <= 32) {
        throw std::runtime_error(
            "Application launch failed: " + std::to_string(error_code)
        );
    }
}

std::ostream& operator<<(std::ostream& stream, const Application& application)
{
    return stream << "Application object: {\n"
                    << "\tUUID: " << application.get_uuid() << "\n"
                    << "\tTitle: " << application.get_file_title() << "\n"
                    << "\tPath: " << application.get_file_path() << " }";
}

void Application::set_executable(std::filesystem::path const &file_path) {
    std::filesystem::path executable_path = std::filesystem::absolute(file_path);

    auto extension = executable_path.extension().wstring();
    std::ranges::transform(extension, extension.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towlower(character));
    });

    const auto name_utf8 = executable_path.stem().u8string();
    std::string application_name(name_utf8.begin(), name_utf8.end());

    if (extension == L".exe" && std::filesystem::is_regular_file(executable_path)) {
        application_name = this->read_application_name(executable_path);
    }

    this->file_path = executable_path;
    this->file_title = application_name;
}

Application::Application() {
    this->set_empty();
}

Application::Application(const std::filesystem::path &executable_path) {
    this->set_executable(executable_path);
}
