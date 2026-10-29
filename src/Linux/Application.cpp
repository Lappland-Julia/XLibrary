#include <memory>
#include <stdexcept>
#include <utility>

#include "Application.h"
#include "LinuxProcessor.h"

std::uint32_t Application::get_pid() const noexcept {
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
}

std::string Application::read_application_name(const std::filesystem::path &executable_path) {
    const auto filename_utf8 = executable_path.stem().u8string();
    const std::string fallback_name(filename_utf8.begin(), filename_utf8.end());

    return fallback_name;
}

std::uint32_t Application::launch(const std::wstring &arguments, const std::filesystem::path &working_directory) {
    const std::filesystem::path executable_path = this->get_file_path();
    if (executable_path.empty()) {
        throw std::invalid_argument("Application executable path is empty");
    }

    auto identity = std::make_shared<xlibrary::process_identity>();
    *identity = xlibrary::launch_process(executable_path, arguments, working_directory);
    this->pid = identity->pid;
    this->process_handle = std::move(identity);
    return this->pid;
}
