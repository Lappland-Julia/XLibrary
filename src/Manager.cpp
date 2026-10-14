#include "Manager.h"

#include <stdexcept>

void Manager::add_applications(const std::vector<Application> &applications) {
    for (auto &app: applications) {
        this->add_application(app);
    }
}

std::uint32_t Manager::launch(const std::string title) const {
    for (auto &app: this->applications) {
        if (app.get_file_title() == title) {
            return app.launch();
        }
    }
    throw std::invalid_argument(title + " is not in this manager");
}

Heartbeat Manager::heartbeat(const std::string title, std::uint32_t process_id) const {
    for (const auto &app: this->applications) {
        if (app.get_file_title() == title) {
            return Heartbeat(process_id, app.get_file_path());
        }
    }
    throw std::invalid_argument(title + " is not in this manager");
}

std::vector<Application> Manager::get_apps() const {
    std::vector<Application> result{};
    for (auto &app: this->applications) {
        result.push_back(app);
    }
    return result;
}

std::vector<std::string> Manager::get_names() const {
    std::vector<std::string> result{};
    for (auto &app: this->applications) {
        result.push_back(app.get_file_title());
    }
    return result;
}

std::vector<std::filesystem::path> Manager::get_paths() const {
    std::vector<std::filesystem::path> result{};
    for (auto &app: this->applications) {
        result.push_back(app.get_file_path());
    }
    return result;
}
