#ifndef XLIBRARY_MANAGER_H
#define XLIBRARY_MANAGER_H

#include <vector>
#include "Application.h"
#include "Heartbeat.h"


class Manager {
private:
    std::vector<Application> applications {};

    static std::string to_lower(std::string data);

public:
    Manager(){};

    explicit Manager(const std::vector<Application> &applications) { this->applications = applications; }
    explicit Manager(const Application &application) { this->applications = {application}; }

    bool app_exists(const Application &application) const;

    void add_application(const Application &application);
    void add_applications(const std::vector<Application> &applications);

    std::uint32_t launch(const std::string title) const;
    Heartbeat heartbeat(const std::string title, std::uint32_t process_id) const;

    std::vector<Application> get_apps() const;
    std::vector<std::string> get_names() const;
    std::vector<std::filesystem::path> get_paths() const;
};


#endif //XLIBRARY_MANAGER_H
