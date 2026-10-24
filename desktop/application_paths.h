#ifndef XLIBRARY_APPLICATION_PATHS_H
#define XLIBRARY_APPLICATION_PATHS_H

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QString>
#include <stdexcept>

namespace application_paths {
inline bool portable_mode() {
    return QFileInfo(QCoreApplication::applicationDirPath() + "/portable.flag").isFile();
}

inline bool bundled_runtime() {
#ifdef Q_OS_LINUX
    if (!portable_mode() || !qEnvironmentVariableIsSet("XLIBRARY_APPIMAGE"))
        return false;
    const auto app_dir = qEnvironmentVariable("APPDIR");
    const QFileInfo bundled_binary(QDir(app_dir).filePath("usr/bin/XLibrary"));
    const auto current_binary = QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath();
    // APPIMAGE может принадлежать другому приложению, запустившему XLibrary.
    if (!QFileInfo(app_dir).isAbsolute() || !QFileInfo(app_dir).isDir() || current_binary.isEmpty() ||
        bundled_binary.canonicalFilePath() != current_binary)
        throw std::runtime_error("Некорректные пути запуска XLibrary AppImage.");
    return true;
#else
    return false;
#endif
}

inline QString appimage_file() {
    if (!bundled_runtime())
        return {};
    const auto image = qEnvironmentVariable("XLIBRARY_APPIMAGE");
    // Извлечённый вручную AppDir использует обычный portable-профиль.
    if (image.isEmpty())
        return {};
    const QFileInfo image_info(image);
    if (!image_info.isAbsolute() || !image_info.isFile() || !image_info.isExecutable())
        throw std::runtime_error("Некорректный путь к файлу XLibrary AppImage.");
    return QDir::cleanPath(image);
}
}

#endif
