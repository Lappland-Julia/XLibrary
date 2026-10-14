#include <windows.h>
#include <iostream>
#include <iomanip>

#include "WindowManager.h"

double WindowManager::get_screen_brightness() const {
    // Размеры только главного монитора
    const int width = GetSystemMetrics(SM_CXSCREEN);
    const int height = GetSystemMetrics(SM_CYSCREEN);

    if (width <= 0 || height <= 0) {
        return -1.0;
    }

    const HDC screenDC = GetDC(nullptr);
    if (!screenDC) {
        return -1.0;
    }

    const HDC memoryDC = CreateCompatibleDC(screenDC);
    if (!memoryDC) {
        ReleaseDC(nullptr, screenDC);
        return -1.0;
    }

    // Создание 32-битного изображения
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* pixels = nullptr;

    HBITMAP bitmap = CreateDIBSection(
        screenDC,
        &info,
        DIB_RGB_COLORS,
        &pixels,
        nullptr,
        0
    );

    if (!bitmap || !pixels) {
        if (bitmap) DeleteObject(bitmap);
        DeleteDC(memoryDC);
        ReleaseDC(nullptr, screenDC);
        return -1.0;
    }

    HGDIOBJ oldBitmap = SelectObject(memoryDC, bitmap);

    if (!oldBitmap || oldBitmap == HGDI_ERROR) {
        DeleteObject(bitmap);
        DeleteDC(memoryDC);
        ReleaseDC(nullptr, screenDC);
        return -1.0;
    }

    // Главный монитор начинается с (0, 0)
    const BOOL captured = BitBlt(
        memoryDC,
        0, 0,
        width, height,
        screenDC,
        0, 0,
        SRCCOPY | CAPTUREBLT
    );

    double average = -1.0;

    if (captured) {
        const auto data = static_cast<const BYTE*>(pixels);
        double total = 0.0;

        const size_t pixelCount =
            static_cast<size_t>(width) * height;

        for (size_t i = 0; i < pixelCount; ++i) {
            const BYTE* pixel = data + i * 4;

            const double b = pixel[0];
            const double g = pixel[1];
            const double r = pixel[2];

            total +=
                0.2126 * r +
                0.7152 * g +
                0.0722 * b;
        }

        average = total / pixelCount;
    }

    // Освобождение ресурсов
    SelectObject(memoryDC, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memoryDC);
    ReleaseDC(nullptr, screenDC);

    return average;
}

void WindowManager::update_theme() {
    const double brightness = this->get_screen_brightness();
    this->brightness = brightness;
    if (brightness>this->threshold) {
        this->dark_theme = false;
    }
    else {
        this->dark_theme = true;
    }
}
