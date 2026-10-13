#include <iostream>
#include "src/Application.h"

int main() {
    const auto steam_app = Application(LR"(C:\Program Files (x86)\Steam\steam.exe)");
    const auto firefox_app = Application(LR"(C:\Program Files\Mozilla Firefox\firefox.exe)");

    std::cout << steam_app << "\n";
    std::cout << firefox_app << "\n";

    int input = 0;
    while (true) {
        std::cin >> input;
        if (input == 1) {
            steam_app.launch();
            break;
        }
    }

    return 0;
}
