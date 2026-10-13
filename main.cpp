#include <iostream>
#include "src/Application.h"
#include "src/Manager.h"

int main() {
    const auto steam_app = Application(LR"(C:\Program Files (x86)\Steam\steam.exe)");
    const auto firefox_app = Application(LR"(C:\Program Files\Mozilla Firefox\firefox.exe)");
    const auto manager = Manager({steam_app, firefox_app});
    std::cout << steam_app << "\n";
    std::cout << firefox_app << "\n";

    std::string input = "";
    while (true) {
        std::cin >> input;
        try {
            manager.launch(input);
            break;
        }
        catch (std::invalid_argument e) {
            std::cerr << e.what() << "\n";
        }
    }

    return 0;
}
