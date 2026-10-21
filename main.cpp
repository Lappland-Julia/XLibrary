#include <cstdlib>
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

#include "desktop/desktop_service.h"
#include "desktop/main_window.h"

int main(int argc, char *argv[]) {
    QApplication application(argc, argv);
    application.setOrganizationName("XLibrary");
    application.setApplicationName("XLibrary");
    application.setApplicationVersion(XLIBRARY_VERSION);
    application.setQuitOnLastWindowClosed(false);
    application.setStyle("Fusion");
    const bool check_startup = application.arguments().contains("--check-startup");
    std::unique_ptr<QTemporaryDir> startup_profile;
    try {
        if (check_startup) {
            startup_profile = std::make_unique<QTemporaryDir>();
            if (!startup_profile->isValid())
                throw std::runtime_error("Не удалось создать временный профиль для проверки запуска.");
            // Database использует LOCALAPPDATA, а настройки Qt получают тот же временный профиль явно.
            if (_wputenv_s(L"LOCALAPPDATA", startup_profile->path().toStdWString().c_str()) != 0)
                throw std::runtime_error("Не удалось настроить временный профиль для проверки запуска.");
        }
        const auto profile = check_startup ? startup_profile->path()
                                           : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        if (!QDir().mkpath(profile))
            throw std::runtime_error("Не удалось создать папку настроек XLibrary.");
        const auto server_name =
            "xlibrary-" + QString::fromLatin1(
                              QCryptographicHash::hash(profile.toUtf8(), QCryptographicHash::Sha256).toHex().left(24));
        QLockFile instance_lock(profile + "/instance.lock");
        instance_lock.setStaleLockTime(0);
        if (!instance_lock.tryLock(0)) {
            QLocalSocket socket;
            socket.connectToServer(server_name);
            if (socket.waitForConnected(1500)) {
                socket.write("show");
                socket.flush();
                socket.waitForBytesWritten(100);
                return 0;
            }
            QMessageBox::information(nullptr, "XLibrary", "Другой экземпляр XLibrary уже запускается или не отвечает.");
            return 1;
        }
        QLocalServer::removeServer(server_name);
        QLocalServer server;
        server.setSocketOptions(QLocalServer::UserAccessOption);
        if (!server.listen(server_name))
            throw std::runtime_error("Не удалось открыть локальный канал XLibrary.");
        DesktopService service(nullptr, profile);
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
            QMessageBox::critical(nullptr, "Ошибка XLibrary", QString::fromUtf8(error.what()));
        return 1;
    }
}
