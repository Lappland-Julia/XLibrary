#include <windows.h>

#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <thread>

#include "src/Backend.h"

int main() try {
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);

    const Database database{false};

    // Example
    /*const auto firefox_app = Application(LR"(C:\Program Files\Mozilla Firefox\firefox.exe)");
    database.add_application(firefox_app);*/

    auto manager = Manager(database.get_applications());
    std::cout << "Available apps: ";
    for (auto name : manager.get_names()) {
        std::cout << name << ";\n";
    }
    std::cout << "\n";

    std::string input = "";
    Heartbeat heartbeat{};
    while (true) {
        std::cout << "Your input: ";
        std::getline(std::cin, input);
        std::cout << "\n";
        if (input == "add") {
            std::getline(std::cin, input);
            std::filesystem::path file_path = std::u8string(input.begin(), input.end());
            auto new_app = Application(file_path);
            if (database.add_application(new_app)) {
                manager.add_application(new_app);
            }
            std::cout << "\n";
        } else if (input == "check") {
            for (auto apps = xlibrary::scan_registry_applications(); auto app : apps) {
                if (database.add_application(app)) {
                    manager.add_application(app);
                }
            }
        } else {
            std::uint32_t process_id = 0;
            try {
                process_id = manager.launch(input);
            } catch (const std::exception &e) {
                std::cerr << e.what() << "\n";
                continue;
            }

            if (process_id == 0) {
                std::cout << "Application opened; process PID is unavailable, monitoring skipped\n";
                return 0;
            }

            std::cout << "Launched process PID: " << process_id << "\n";
            try {
                heartbeat = manager.heartbeat(input, process_id);
            } catch (const std::exception &e) {
                std::cerr << "Application opened, but process monitoring failed: " << e.what() << "\n";
                return 0;
            }
            break;
        }
    }

    bool alive = heartbeat.is_alive();
    while (alive) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        alive = heartbeat.is_alive();
    }
    if (const auto error_code = heartbeat.get_error_code(); error_code != 0) {
        std::cerr << "Cannot check process: " << error_code << "\n";
        return 1;
    }

    std::cout << "Process finished\n";

    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << "\n";
    return 1;
}
