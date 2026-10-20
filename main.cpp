#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QMessageBox>
#include <QStandardPaths>

#include "desktop/desktop_service.h"
#include "desktop/main_window.h"

int main(int argc, char *argv[]) {
    QApplication application(argc, argv);
    application.setOrganizationName("XLibrary");
    application.setApplicationName("XLibrary");
    application.setApplicationVersion("0.1.0");
    application.setQuitOnLastWindowClosed(false);
    application.setStyle("Fusion");
    try {
        const auto profile = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
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
        DesktopService service;
        MainWindow window(service);
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
        QMessageBox::critical(nullptr, "Ошибка XLibrary", QString::fromUtf8(error.what()));
        return 1;
    }
}
