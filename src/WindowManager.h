#ifndef XLIBRARY_WINDOWMANAGER_H
#define XLIBRARY_WINDOWMANAGER_H

// Устаревший совместимый API оценки темы по яркости экрана. GUI применяет тему
// через desktop/appearance и desktop_platform, не используя этот класс.
class WindowManager {
private:
    bool dark_theme = true;
    double threshold = 150.0;
    double brightness = -1;

    double get_screen_brightness() const;

public:
    void update_theme();
    int get_theme() const { return this->dark_theme; }
};

#endif //XLIBRARY_WINDOWMANAGER_H
