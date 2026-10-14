#include <chrono>
#include <exception>
#include <iostream>
#include <thread>
#include "src/Backend.h"

int main() try {
    const Database database{false};
    auto path = database.setup();
    const auto steam_app = Application(LR"(C:\Program Files (x86)\Steam\steam.exe)");
    const auto firefox_app = Application(LR"(C:\Program Files\Mozilla Firefox\firefox.exe)");

    database.add_application(firefox_app);
    database.add_application(steam_app);

    const auto manager = Manager(database.get_applications());
    std::cout << "Available apps: ";
    for (auto name: manager.get_names()) {
        std::cout << name << "; ";
    }
    std::cout << "\n";

    std::string input = "";
    Heartbeat heartbeat {};
    while (true) {
        std::cout << "Your input: ";
        std::cin >> input;
        std::cout << "\n";
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
