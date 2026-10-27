#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFileInfo>
#include <qtkeychain/keychain.h>

#include "github_token_store.h"

namespace {
constexpr auto token_setting = "updates/github_token_saved";
}

GithubTokenStore::GithubTokenStore(QSettings &settings, QObject *parent) : QObject(parent), settings(settings) {
    // Разные локальные профили не должны перезаписывать токены друг друга.
    const auto profile = QFileInfo(settings.fileName()).absoluteFilePath().toUtf8();
    key =
        "github-updates/" + QString::fromLatin1(QCryptographicHash::hash(profile, QCryptographicHash::Sha256).toHex());
}

bool GithubTokenStore::has_token() const {
    return settings.value(token_setting, false).toBool();
}

void GithubTokenStore::start_job(QKeychain::Job *job, operation action) {
    job->setKey(key);
    job->setInsecureFallback(false);
    busy = true;
    emit busy_changed();
    connect(job, &QKeychain::Job::finished, this, [this, action](QKeychain::Job *finished) {
        busy = false;
        const bool missing = finished->error() == QKeychain::EntryNotFound;
        if (missing && action == operation::read) {
            emit busy_changed();
            emit failed(QCoreApplication::translate(
                "XLibrary",
                "The saved GitHub token was not found. Save a new token or delete it from the update settings."));
            return;
        }
        if (finished->error() != QKeychain::NoError && !(missing && action == operation::remove)) {
            emit busy_changed();
            emit failed(QCoreApplication::translate(
                "XLibrary",
                "Could not access the system secret store. Unlock it or check that a secret service is running."));
            return;
        }
        if (action == operation::read) {
            const auto token = static_cast<QKeychain::ReadPasswordJob *>(finished)->textData();
            emit busy_changed();
            emit read_finished(token);
            return;
        }
        settings.setValue(token_setting, action == operation::write);
        settings.sync();
        emit busy_changed();
        if (settings.status() != QSettings::NoError) {
            emit failed(QCoreApplication::translate(
                "XLibrary",
                "The system secret store was updated, but the settings file could not be saved. Please try again."));
            return;
        }
        emit changed(action == operation::write);
    });
    job->start();
}

void GithubTokenStore::read() {
    if (busy)
        return;
    // Без настроенного токена проверка не требует доступного или разблокированного хранилища.
    if (!has_token()) {
        emit read_finished({});
        return;
    }
    start_job(new QKeychain::ReadPasswordJob("XLibrary", this), operation::read);
}

void GithubTokenStore::save(const QString &token) {
    if (busy || token.isEmpty())
        return;
    auto *job = new QKeychain::WritePasswordJob("XLibrary", this);
    job->setTextData(token);
    start_job(job, operation::write);
}

void GithubTokenStore::remove() {
    if (!busy)
        start_job(new QKeychain::DeletePasswordJob("XLibrary", this), operation::remove);
}
