#include <chrono>
#include <exception>
#include <iostream>
#include <thread>
#include "src/Backend.h"

int main() try {
    const Database database{false};
    database.setup();

    // Example
    /*const auto firefox_app = Application(LR"(C:\Program Files\Mozilla Firefox\firefox.exe)");
    database.add_application(firefox_app);*/

    auto manager = Manager(database.get_applications());
    std::cout << "Available apps: ";
    for (auto name: manager.get_names()) {
        std::cout << name << "; ";
    }
    std::cout << "\n";

    std::string input = "";
    Heartbeat heartbeat {};
    while (true) {
        std::cout << "Your input: ";
        std::getline(std::cin, input);
        std::cout << "\n";
        if (input == "add") {
            std::getline(std::cin, input);
            std::filesystem::path file_path = input;
            auto new_app = Application(file_path);
            database.add_application(new_app);
            manager.add_application(new_app);
            std::cout << "\n";
        }
        else {
            try {
                const auto process_id = manager.launch(input);
                std::cout << "Launched process PID: " << process_id << "\n";
                heartbeat = manager.heartbeat(input, process_id);
                break;
            }
            catch (const std::exception &e) {
                std::cerr << e.what() << "\n";
            }
        }
    }

    bool alive = heartbeat.is_alive();
    while (alive) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        alive = heartbeat.is_alive();
    }
    if (!alive) {
        std::cout << "Process finished\n";
    }

    if (const auto error_code = heartbeat.get_error_code(); error_code != 0) {
        std::cerr << "Cannot check process: " << error_code << "\n";
        return 1;
    }

    return 0;
}
catch (const std::exception &error) {
    std::cerr << error.what() << "\n";
    return 1;
}
