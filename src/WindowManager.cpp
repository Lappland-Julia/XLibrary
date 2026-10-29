#include "WindowManager.h"

void WindowManager::update_theme() {
    const double brightness = this->get_screen_brightness();
    this->brightness = brightness;
    if (brightness > this->threshold) {
        this->dark_theme = false;
    } else {
        this->dark_theme = true;
    }
}
