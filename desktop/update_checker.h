#ifndef XLIBRARY_UPDATE_CHECKER_H
#define XLIBRARY_UPDATE_CHECKER_H

#include <memory>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

class QNetworkReply;

class UpdateChecker : public QObject {
    Q_OBJECT
private:
    QNetworkAccessManager network;
    QTimer deadline;
    QNetworkReply *reply = nullptr;
    QByteArray response;
    QJsonObject latest_release;
    int release_page = 0;
    bool busy = false;
    bool downloading = false;
    QString release_tag;
    QUrl release_url;
    QUrl installer_url;
    QByteArray installer_digest;
    qint64 installer_size = 0;
    std::unique_ptr<QTemporaryDir> download_directory;
    QFile installer;
    QCryptographicHash download_hash{QCryptographicHash::Sha256};
    void request_releases();
    void read_response();
    void finish_reply();
    void select_release();
    void stop_request();
    void fail(const QString &message);

public:
    explicit UpdateChecker(QObject *parent = nullptr);
    ~UpdateChecker() override;
    bool is_busy() const { return busy; }
    QString available_version() const { return release_tag; }
    QUrl available_release_url() const { return release_url; }
    bool can_install() const;
    void check();
    void download();
    void cancel();
    bool launch_installer(QString &error);

signals:
    void busy_changed();
    void update_available();
    void up_to_date(bool releases_exist);
    void failed(const QString &message);
    void download_progress(qint64 received, qint64 total);
    void installer_ready();
};

#endif
