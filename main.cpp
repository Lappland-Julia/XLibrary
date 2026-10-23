#include <memory>
#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>

#include "desktop/appearance.h"
#include "desktop/desktop_service.h"
#include "desktop/main_window.h"

int main(int argc, char *argv[]) {
    QApplication application(argc, argv);
    application.setOrganizationName("XLibrary");
    application.setApplicationName("XLibrary");
    application.setApplicationVersion(XLIBRARY_VERSION);
    application.setWindowIcon(appearance::logo());
    application.setQuitOnLastWindowClosed(false);
    application.setStyle("Fusion");
    const bool check_startup = application.arguments().contains("--check-startup");
    const bool portable = QFileInfo(application.applicationDirPath() + "/portable.flag").isFile();
    std::unique_ptr<QTemporaryDir> startup_profile;
    try {
        if (check_startup) {
            startup_profile = std::make_unique<QTemporaryDir>();
            if (!startup_profile->isValid())
                throw std::runtime_error("Не удалось создать временный профиль для проверки запуска.");
        }
        const auto profile = check_startup ? startup_profile->path()
                             : portable    ? application.applicationDirPath() + "/data"
                                           : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        if (!QDir().mkpath(profile))
            throw std::runtime_error("Не удалось создать папку настроек XLibrary.");
        const auto server_name =
            "xlibrary-" + QString::fromLatin1(
                              QCryptographicHash::hash(profile.toUtf8(), QCryptographicHash::Sha256).toHex().left(24));
        QLockFile instance_lock(profile + "/instance.lock");
        instance_lock.setStaleLockTime(0);
        if (!instance_lock.tryLock(0)) {
            if (instance_lock.error() != QLockFile::LockFailedError)
                throw std::runtime_error(
                    "Не удалось записать файл блокировки профиля XLibrary. Проверьте права доступа.");
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
        // Portable и проверка запуска используют свой каталог без подмены окружения дочерних приложений.
        const auto database_path = (portable || check_startup) ? profile + "/xlibrary.db" : QString{};
        DesktopService service(nullptr, profile, database_path);
        if (!service.settings.isWritable())
            throw std::runtime_error("Нет прав на запись настроек XLibrary. Выберите доступную для записи папку.");
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
