#include "WindowManager.h"

double WindowManager::get_screen_brightness() const {
    // Wayland does not provide screen capture without the user's permission.
    return -1.0;
}
