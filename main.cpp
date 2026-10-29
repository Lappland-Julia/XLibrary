#include <memory>
#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>

#include "desktop/appearance.h"
#include "desktop/application_paths.h"
#include "desktop/desktop_service.h"
#include "desktop/localization.h"
#include "desktop/main_window.h"

int main(int argc, char *argv[]) {
    QApplication application(argc, argv);
    Localization localization;
    localization.apply();
    application.setOrganizationName("XLibrary");
    application.setApplicationName("XLibrary");
    application.setApplicationVersion(XLIBRARY_VERSION);
    application.setWindowIcon(appearance::logo());
    application.setQuitOnLastWindowClosed(false);
    application.setStyle("Fusion");
    const bool check_startup = application.arguments().contains("--check-startup");
    const bool portable = application_paths::portable_mode();
    std::unique_ptr<QTemporaryDir> startup_profile;
    try {
        if (check_startup) {
            startup_profile = std::make_unique<QTemporaryDir>();
            if (!startup_profile->isValid())
                throw std::runtime_error(QCoreApplication::translate(
                                             "XLibrary", "Could not create a temporary profile for the startup check.")
                                             .toStdString());
        }
        const auto appimage = check_startup ? QString{} : application_paths::appimage_file();
        const auto profile = check_startup         ? startup_profile->path()
                             : !appimage.isEmpty() ? appimage + ".data"
                             : portable            ? application.applicationDirPath() + "/data"
                                        : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        if (!QDir().mkpath(profile))
            throw std::runtime_error(
                QCoreApplication::translate("XLibrary", "Could not create the XLibrary settings folder.")
                    .toStdString());
        QSettings language_settings(profile + "/settings.ini", QSettings::IniFormat);
        localization.apply(language_settings.value("interface/language", "system").toString());
        const auto server_name =
            "xlibrary-" + QString::fromLatin1(
                              QCryptographicHash::hash(profile.toUtf8(), QCryptographicHash::Sha256).toHex().left(24));
        QLockFile instance_lock(profile + "/instance.lock");
        instance_lock.setStaleLockTime(0);
        if (!instance_lock.tryLock(0)) {
            if (instance_lock.error() != QLockFile::LockFailedError)
                throw std::runtime_error(
                    QCoreApplication::translate(
                        "XLibrary", "Could not write the XLibrary profile lock file. Check access permissions.")
                        .toStdString());
            QLocalSocket socket;
            socket.connectToServer(server_name);
            if (socket.waitForConnected(1500)) {
                socket.write("show");
                socket.flush();
                socket.waitForBytesWritten(100);
                return 0;
            }
            QMessageBox::information(
                nullptr, "XLibrary",
                QCoreApplication::translate("XLibrary", "Another instance of XLibrary is starting or not responding."));
            return 1;
        }
        QLocalServer::removeServer(server_name);
        QLocalServer server;
        server.setSocketOptions(QLocalServer::UserAccessOption);
        if (!server.listen(server_name))
            throw std::runtime_error(
                QCoreApplication::translate("XLibrary", "Could not open the XLibrary local channel.").toStdString());
        // Portable mode and startup checks use their own directory without replacing the child process environment.
        const auto database_path = (portable || check_startup) ? profile + "/xlibrary.db" : QString{};
        DesktopService service(nullptr, profile, database_path);
        if (!service.settings.isWritable())
            throw std::runtime_error(
                QCoreApplication::translate("XLibrary", "Cannot write XLibrary settings. Choose a writable folder.")
                    .toStdString());
        MainWindow window(service);
        if (check_startup) {
            QObject::connect(&service, &DesktopService::catalog_changed, &application, [&] {
                if (service.is_ready()) {
                    QTimer::singleShot(0, &application, [&] { application.exit(window.isVisible() ? 0 : 1); });
                }
            });
            QObject::connect(&service, &DesktopService::message, &application, [&](const QString &message) {
                qCritical("%s", qPrintable(message));
                application.exit(1);
            });
            QTimer::singleShot(30000, &application, [&] {
                qCritical("Startup check timed out.");
                application.exit(1);
            });
        }
        QObject::connect(&server, &QLocalServer::newConnection, &window, [&] {
            while (auto *socket = server.nextPendingConnection()) {
                window.show_library();
                socket->disconnectFromServer();
                socket->deleteLater();
            }
        });
        window.start();
        return application.exec();
    } catch (const std::exception &error) {
        if (check_startup)
            qCritical("%s", error.what());
        else
            QMessageBox::critical(nullptr, QCoreApplication::translate("XLibrary", "XLibrary error"),
                                  QString::fromUtf8(error.what()));
        return 1;
    }
}
