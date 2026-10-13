#ifndef XLIBRARY_MANAGER_H
#define XLIBRARY_MANAGER_H

#include <vector>
#include "Application.h"


class Manager {
private:
    std::vector<Application> applications {};

public:
    Manager(){};

    explicit Manager(const std::vector<Application> &applications) { this->applications = applications; }
    explicit Manager(const Application &application) { this->applications = {application}; }

    void add_application(const Application &application) { this->applications.push_back(application); }
    void add_applications(const std::vector<Application> &applications);

    void launch(const std::string title) const;
};


#endif //XLIBRARY_MANAGER_H
