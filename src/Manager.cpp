#include "Manager.h"

void Manager::add_applications(const std::vector<Application> &applications) {
    for (auto &app: applications) {
        this->add_application(app);
    }
}

void Manager::launch(const std::string title) const {
    for (auto &app: this->applications) {
        if (app.get_file_title() == title) {
            return app.launch();
        }
    }
    throw std::invalid_argument(title + " is not in this manager");
}
