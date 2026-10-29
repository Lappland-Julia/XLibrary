#include <iomanip>
#include <ostream>
#include <stdexcept>

#include "Application.h"

std::uint32_t Application::launch() {
    return this->launch({}, this->file_path.parent_path());
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

    const auto application_name = this->read_application_name(executable_path);

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
