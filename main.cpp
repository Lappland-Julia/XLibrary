#include <chrono>
#include <iostream>
#include <thread>
#include "src/Application.h"
#include "src/Manager.h"
#include "src/WindowManager.h"

int main() {

    auto wm = WindowManager();
    wm.update_theme();
    std::cout << wm.get_theme();
    return 0;

    const auto steam_app = Application(LR"(C:\Program Files (x86)\Steam\steam.exe)");
    const auto firefox_app = Application(LR"(C:\Program Files\Mozilla Firefox\firefox.exe)");
    const auto manager = Manager({steam_app, firefox_app});

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
