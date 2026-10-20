#include <algorithm>
#include <cctype>
#include <stdexcept>

#include "Manager.h"

std::string Manager::to_lower(std::string data) {
    std::ranges::transform(data, data.begin(), [](unsigned char c) { return std::tolower(c); });
    return data;
}

bool Manager::app_exists(const Application &application) const {
    for (auto &app : this->applications) {
        if (app == application) {
            return true;
        }
    }
    return false;
}

void Manager::add_application(const Application &application) {
    if (!this->app_exists(application)) {
        this->applications.push_back(application);
    }
}

void Manager::add_applications(const std::vector<Application> &applications) {
    for (auto &app : applications) {
        this->add_application(app);
    }
}

std::uint32_t Manager::launch(const std::string title) const {
    for (auto &app : this->applications) {
        if (this->to_lower(app.get_file_title()) == this->to_lower(title)) {
            return app.launch();
        }
    }
    throw std::invalid_argument(title + " is not in this manager");
}

Heartbeat Manager::heartbeat(const std::string title, std::uint32_t process_id) const {
    for (const auto &app : this->applications) {
        if (this->to_lower(app.get_file_title()) == this->to_lower(title)) {
            return Heartbeat(process_id, app.get_file_path());
        }
    }
    throw std::invalid_argument(title + " is not in this manager");
}

std::vector<Application> Manager::get_apps() const {
    std::vector<Application> result{};
    for (auto &app : this->applications) {
        result.push_back(app);
    }
    return result;
}

std::vector<std::string> Manager::get_names() const {
    std::vector<std::string> result{};
    for (auto &app : this->applications) {
        result.push_back(app.get_file_title());
    }
    return result;
}

std::vector<std::filesystem::path> Manager::get_paths() const {
    std::vector<std::filesystem::path> result{};
    for (auto &app : this->applications) {
        result.push_back(app.get_file_path());
    }
    return result;
}
