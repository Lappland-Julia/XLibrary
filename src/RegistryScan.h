#ifndef XLIBRARY_REGISTRY_SCAN_H
#define XLIBRARY_REGISTRY_SCAN_H

#include <cstdint>
#include <string>
#include <vector>

#include "Application.h"

namespace xlibrary {

enum class registry_scope { current_user, local_machine };

struct registry_source {
    registry_scope scope;
    std::uint32_t view; // Флаг KEY_WOW64_32KEY или KEY_WOW64_64KEY.
    std::wstring key;
};

struct scan_issue {
    registry_source source;
    std::wstring operation;
    std::int32_t error; // Код ошибки Win32.
};

// Сведения об установке не являются объектами Application для запуска.
struct uninstall_entry {
    std::wstring display_name;
    std::wstring display_version;
    std::wstring publisher;
    std::wstring install_location;
    std::wstring display_icon;
    std::wstring uninstall_command;
    std::wstring quiet_uninstall_command;
    registry_source source;
};

// Если issues != nullptr, ошибки дописываются в переданный вектор.
// Исключения конструктора Application не подавляются.
std::vector<::Application> scan_app_paths_current_user(std::vector<scan_issue> *issues = nullptr);
std::vector<::Application> scan_app_paths_local_machine(std::vector<scan_issue> *issues = nullptr);
std::vector<::Application> scan_registry_applications(std::vector<scan_issue> *issues = nullptr);

// Отдельные списки: никогда не объединяются со списком Application.
std::vector<uninstall_entry> scan_uninstall_current_user(std::vector<scan_issue> *issues = nullptr);
std::vector<uninstall_entry> scan_uninstall_local_machine(std::vector<scan_issue> *issues = nullptr);

} // namespace xlibrary

#endif // XLIBRARY_REGISTRY_SCAN_H
