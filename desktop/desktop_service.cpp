#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QtConcurrent/QtConcurrentRun>
#include <QUrl>

#include "catalog_store.h"
#include "desktop_service.h"
#include "localization.h"
#include "path_helpers.h"
#include "platform_integration.h"

namespace {
QString discovery_summary(qsizetype added, const QVector<discovery_issue> &issues, const QString &status) {
    qsizetype duplicates = 0;
    qsizetype skipped = 0;
    qsizetype errors = 0;
    for (const auto &issue : issues) {
        switch (issue.kind) {
        case discovery_issue_kind::duplicate:
            ++duplicates;
            break;
        case discovery_issue_kind::skipped:
            ++skipped;
            break;
        case discovery_issue_kind::error:
            ++errors;
            break;
        }
    }
    return QCoreApplication::translate("XLibrary", "%1 Added: %2. Duplicates: %3. Skipped: %4. Errors: %5.")
        .arg(status)
        .arg(added)
        .arg(duplicates)
        .arg(skipped)
        .arg(errors);
}

QString discovery_details(const QString &summary, const QVector<discovery_issue> &issues) {
    QStringList details{summary};
    for (const auto &issue : issues) {
        const auto kind =
            issue.kind == discovery_issue_kind::duplicate ? QCoreApplication::translate("XLibrary", "Duplicate")
            : issue.kind == discovery_issue_kind::skipped ? QCoreApplication::translate("XLibrary", "Skipped")
                                                          : QCoreApplication::translate("XLibrary", "Error");
        details.append(QString("[%1] %2: %3").arg(kind, issue.source, issue.reason));
    }
    return details.join('\n');
}
}

class CatalogWorker : public QObject {
    Q_OBJECT
    QString database_path;

public:
    explicit CatalogWorker(const QString &database_path) : database_path(database_path) {}
    std::unique_ptr<CatalogStore> store;
    catalog_snapshot catalog;
    QTimer *monitor = nullptr;
    QSet<QString> last_running;
    QSet<QString> last_unavailable;
    bool monitor_error = false;

    void initialize() {
        try {
            if (database_path.isEmpty())
                database_path = path_text(Database::get_database_path());
            emit location_changed(database_path);
            store.reset();
            store = std::make_unique<CatalogStore>(database_path);
            refresh();
            monitor = new QTimer(this);
            monitor->setInterval(2500);
            connect(monitor, &QTimer::timeout, this, [this] { poll(); });
            monitor->start();
        } catch (const std::exception &error) {
            store.reset();
            emit initialization_failed(QCoreApplication::translate("XLibrary", "Could not open the library: %1")
                                           .arg(QString::fromUtf8(error.what())));
        }
    }
    void restore(const QString &path) {
        try {
            if (database_path.isEmpty())
                database_path = path_text(Database::get_database_path());
            store.reset();
            const auto preserved = CatalogStore::restore_backup(path, database_path);
            initialize();
            if (store)
                emit notification(preserved.isEmpty()
                                      ? QCoreApplication::translate("XLibrary", "Library restored.")
                                      : QCoreApplication::translate("XLibrary", "Library restored. Previous files: %1")
                                            .arg(preserved));
        } catch (const std::exception &error) {
            emit initialization_failed(QString::fromUtf8(error.what()));
        }
    }
    void refresh() {
        catalog = store->read();
        for (auto &record : catalog.applications)
            record.running = last_running.contains(normalized_path(record.path));
        emit loaded(catalog);
        poll();
    }
    void poll() {
        QSet<QString> unavailable;
        for (auto &record : catalog.applications) {
            record.available = QFileInfo(record.path).isFile();
            if (!record.available)
                unavailable.insert(normalized_path(record.path));
        }
        auto paths = last_running;
        try {
            paths = desktop_platform::running_paths(catalog.applications);
            monitor_error = false;
        } catch (const std::exception &error) {
            if (!monitor_error)
                emit failed(QString::fromUtf8(error.what()));
            monitor_error = true;
        }
        for (auto &record : catalog.applications)
            record.running = paths.contains(normalized_path(record.path));
        if (paths != last_running || unavailable != last_unavailable) {
            last_running = paths;
            last_unavailable = unavailable;
            emit state_changed(paths, unavailable);
        }
    }

signals:
    void loaded(const catalog_snapshot &catalog);
    void initialization_failed(const QString &message);
    void location_changed(const QString &path);
    void state_changed(const QSet<QString> &paths, const QSet<QString> &unavailable);
    void failed(const QString &message);
    void completed(const QString &operation, bool success);
    void resolved(const QString &requested_path, const app_record &record);
    void scenario_saved(qint64 id);
    void tag_renamed(const QString &old_name, const QString &new_name);
    void launched(qint64 id, bool success, quint64 scenario);
    void notification(const QString &message);
    void discovery_report_ready(const QString &report);
};

DesktopService::DesktopService(QObject *parent, const QString &profile_path, const QString &database_path)
    : QObject(parent), worker(new CatalogWorker(database_path)), model(this), filter(this),
      settings((profile_path.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                                       : profile_path) +
                   "/settings.ini",
               QSettings::IniFormat) {
    filter.setSourceModel(&model);
    discovery_pool.setMaxThreadCount(1);
    discovery_pool.setExpiryTimeout(5000);
    worker->moveToThread(&worker_thread);
    connect(&worker_thread, &QThread::started, worker, &CatalogWorker::initialize);
    connect(&worker_thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(worker, &CatalogWorker::location_changed, this, [this](const QString &path) { library_file = path; });
    connect(worker, &CatalogWorker::initialization_failed, this, [this](const QString &error) {
        opening = false;
        ready = false;
        opening_error = error;
        emit catalog_changed();
        emit message(error);
    });
    connect(worker, &CatalogWorker::failed, this, &DesktopService::message);
    connect(worker, &CatalogWorker::notification, this, &DesktopService::message);
    connect(worker, &CatalogWorker::discovery_report_ready, this, [this](const QString &report) {
        scan_report = report;
        emit discovery_report_changed();
    });
    connect(worker, &CatalogWorker::completed, this, &DesktopService::operation_finished);
    connect(this, &DesktopService::operation_finished, this, [this](const QString &operation, bool success) {
        if (operation == "discover") {
            if (success) {
                scan_progress.percent = 100;
                scan_progress.stage = QCoreApplication::translate("XLibrary", "Scan complete");
            }
            discovering = false;
            emit discovery_changed();
        }
    });
    connect(worker, &CatalogWorker::resolved, this, &DesktopService::application_resolved);
    connect(worker, &CatalogWorker::scenario_saved, this, &DesktopService::scenario_saved);
    connect(worker, &CatalogWorker::tag_renamed, this, [this](const QString &old_name, const QString &new_name) {
        if (!old_name.isEmpty() && current_tag == old_name)
            navigate(current_section, new_name);
    });
    connect(worker, &CatalogWorker::loaded, this, [this](const catalog_snapshot &value) {
        snapshot = value;
        if (!current_tag.isEmpty() && !snapshot.tags.contains(current_tag))
            navigate(current_section);
        ready = true;
        opening = false;
        opening_error.clear();
        model.replace(value.applications);
        if (!model.find(selected_id))
            selected_id = 0;
        emit catalog_changed();
        emit selection_changed();
    });
    connect(worker, &CatalogWorker::state_changed, this,
            [this](const QSet<QString> &paths, const QSet<QString> &unavailable) {
                for (auto &record : snapshot.applications) {
                    const auto path = normalized_path(record.path);
                    record.running = paths.contains(path);
                    record.available = !unavailable.contains(path);
                }
                model.update_state(paths, unavailable);
                emit selection_changed();
            });
    connect(worker, &CatalogWorker::launched, this, [this](qint64 id, bool success, quint64 from_scenario) {
        launching.remove(id);
        if (success && !from_scenario)
            emit launch_succeeded();
        if (!from_scenario || !scenario_active || from_scenario != scenario_generation->load())
            return;
        if (!success) {
            cancel_scenario();
            emit scenario_changed(QCoreApplication::translate(
                "XLibrary", "The scenario stopped because an application could not be launched."));
            return;
        }
        ++step_index;
        next_scenario_step();
    });
    scenario_timer.setSingleShot(true);
    connect(&scenario_timer, &QTimer::timeout, this, [this] {
        if (scenario_active && step_index < pending_steps.size()) {
            launch(pending_steps[step_index].application_id, false, true);
        }
    });
    worker_thread.setObjectName("catalog_worker");
    worker_thread.start();
}

DesktopService::~DesktopService() {
    ++*scenario_generation;
    auto expected = discovery_phase::running;
    discovery_cancelled->phase.compare_exchange_strong(expected, discovery_phase::cancelling);
    discovery_cancelled->requested.store(true);
    scenario_timer.stop();
    discovery_pool.clear();
    discovery_pool.waitForDone();
    worker_thread.quit();
    worker_thread.wait();
}

void DesktopService::retry_library() {
    if (ready || opening)
        return;
    opening = true;
    opening_error.clear();
    emit catalog_changed();
    QMetaObject::invokeMethod(worker, &CatalogWorker::initialize, Qt::QueuedConnection);
}

void DesktopService::open_profile_folder() {
    enqueue("folder", [path = settings.fileName()](CatalogWorker &) { desktop_platform::open_folder(path); });
}

void DesktopService::restore_library(const QString &path) {
    if (ready || opening || path.isEmpty())
        return;
    opening = true;
    opening_error.clear();
    emit catalog_changed();
    QMetaObject::invokeMethod(worker, [worker = worker, path] { worker->restore(path); }, Qt::QueuedConnection);
}

void DesktopService::enqueue(const QString &operation, std::function<void(CatalogWorker &)> task) {
    QMetaObject::invokeMethod(
        worker,
        [worker = worker, operation, task = std::move(task)] {
            try {
                if (!worker->store && operation != "folder")
                    throw std::runtime_error(
                        QCoreApplication::translate(
                            "XLibrary",
                            "The library is unavailable. Restart the application after resolving the error.")
                            .toStdString());
                task(*worker);
                emit worker->completed(operation, true);
            } catch (const DiscoveryCancelled &) {
                emit worker->completed(operation, false);
            } catch (const std::exception &error) {
                emit worker->failed(QString::fromUtf8(error.what()));
                emit worker->completed(operation, false);
            }
        },
        Qt::QueuedConnection);
}

void DesktopService::select(qint64 id) {
    if (selected_id != id) {
        selected_id = id;
        emit selection_changed();
    }
}

void DesktopService::navigate(const QString &section, const QString &tag) {
    current_section = section;
    current_tag = tag;
    filter.set_section(section, tag);
    emit navigation_changed();
}

void DesktopService::save_application(app_record record) {
    enqueue("application", [record = std::move(record)](CatalogWorker &worker) {
        worker.store->save_application(record);
        worker.refresh();
    });
}

void DesktopService::resolve_application(const QString &path) {
    enqueue("resolve",
            [path](CatalogWorker &worker) { emit worker.resolved(path, desktop_platform::resolve_application(path)); });
}

void DesktopService::remove_application(qint64 id) {
    enqueue("remove_application", [id](CatalogWorker &worker) {
        worker.store->remove_application(id);
        worker.refresh();
    });
}

void DesktopService::save_tag(const QString &old_name, const QString &new_name) {
    enqueue("tag", [old_name, new_name](CatalogWorker &worker) {
        worker.store->save_tag(old_name, new_name);
        emit worker.tag_renamed(old_name, new_name);
        worker.refresh();
    });
}

void DesktopService::remove_tag(const QString &name) {
    enqueue("remove_tag", [name](CatalogWorker &worker) {
        worker.store->remove_tag(name);
        worker.refresh();
    });
}

void DesktopService::save_scenario(scenario_record record) {
    enqueue("scenario", [record = std::move(record)](CatalogWorker &worker) {
        const auto id = worker.store->save_scenario(record);
        worker.refresh();
        emit worker.scenario_saved(id);
    });
}

void DesktopService::remove_scenario(qint64 id) {
    enqueue("remove_scenario", [id](CatalogWorker &worker) {
        worker.store->remove_scenario(id);
        worker.refresh();
    });
}

void DesktopService::launch(qint64 id, bool new_instance, bool scenario) {
    if (!ready || !id)
        return;
    if (launching.contains(id)) {
        if (scenario) {
            cancel_scenario();
            notify(QCoreApplication::translate(
                "XLibrary", "The application is still starting. Try the scenario again after it has started."));
        }
        return;
    }
    launching.insert(id);
    const auto generation = scenario ? scenario_generation->load() : 0;
    const auto mode = new_instance ? desktop_platform::launch_mode::new_instance
                      : settings.value("behavior/relaunch", "activate").toString() == "activate"
                          ? desktop_platform::launch_mode::activate
                          : desktop_platform::launch_mode::launch;
    enqueue("launch", [id, mode, generation, token = scenario_generation](CatalogWorker &worker) {
        try {
            if (generation && token->load() != generation) {
                emit worker.launched(id, false, generation);
                return;
            }
            const auto it = std::find_if(worker.catalog.applications.cbegin(), worker.catalog.applications.cend(),
                                         [id](const auto &record) { return record.id == id; });
            if (it == worker.catalog.applications.cend())
                throw std::runtime_error(
                    QCoreApplication::translate("XLibrary", "The application was not found in the library.")
                        .toStdString());
            desktop_platform::launch_application(*it, mode);
            // A history error must not turn a successful launch into a failed or repeated launch.
            emit worker.launched(id, true, generation);
            try {
                worker.store->record_launch(id);
                worker.refresh();
            } catch (const std::exception &error) {
                emit worker.failed(
                    QCoreApplication::translate("XLibrary",
                                                "The application opened, but its launch history could not be saved: %1")
                        .arg(QString::fromUtf8(error.what())));
            }
        } catch (...) {
            emit worker.launched(id, false, generation);
            throw;
        }
    });
}

void DesktopService::open_folder(qint64 id) {
    if (const auto *record = model.find(id))
        enqueue("folder", [path = record->path](CatalogWorker &) { desktop_platform::open_folder(path); });
}

void DesktopService::run_scenario(qint64 id) {
    if (scenario_active) {
        notify(QCoreApplication::translate("XLibrary", "A scenario is already running. Stop it first."));
        return;
    }
    for (const auto &record : snapshot.scenarios) {
        if (record.id == id) {
            pending_steps.clear();
            for (const auto &step : record.steps)
                if (step.enabled)
                    pending_steps.append(step);
            if (pending_steps.isEmpty()) {
                notify(QCoreApplication::translate("XLibrary", "The scenario has no enabled applications."));
                return;
            }
            scenario_name = record.name;
            ++*scenario_generation;
            step_index = 0;
            scenario_active = true;
            next_scenario_step();
            return;
        }
    }
}

void DesktopService::next_scenario_step() {
    if (step_index >= pending_steps.size()) {
        scenario_active = false;
        emit scenario_changed(QCoreApplication::translate("XLibrary", "Scenario “") + scenario_name +
                              QCoreApplication::translate("XLibrary", "” completed."));
        return;
    }
    emit scenario_changed(QCoreApplication::translate("XLibrary", "%1 · step %2 of %3")
                              .arg(scenario_name)
                              .arg(step_index + 1)
                              .arg(pending_steps.size()));
    scenario_timer.start(pending_steps[step_index].delay_ms);
}

void DesktopService::cancel_scenario() {
    ++*scenario_generation;
    scenario_active = false;
    scenario_timer.stop();
    emit scenario_changed(QCoreApplication::translate(
        "XLibrary", "The scenario stopped. Applications already opened will keep running."));
}

void DesktopService::discover() {
    if (discovering || !ready)
        return;
    const bool registry = settings.value("sources/registry", true).toBool();
    const bool start_menu = settings.value("sources/start_menu", true).toBool();
    if (!registry && !start_menu) {
        notify(QCoreApplication::translate("XLibrary", "Select discovery sources in the library settings."));
        return;
    }
    discovering = true;
    discovery_cancelled = std::make_shared<discovery_control>();
    scan_progress = {-1, 0, 0, QCoreApplication::translate("XLibrary", "Preparing the application list…")};
    emit discovery_changed();
    // Updates from both worker threads reach the UI only when the percentage or stage changes.
    const std::function<void(const discovery_progress &)> report_progress =
        [this, cancelled = discovery_cancelled, last_percent = -2,
         last_stage = QString{}](const discovery_progress &progress) mutable {
            if (progress.percent == last_percent && progress.stage == last_stage)
                return;
            last_percent = progress.percent;
            last_stage = progress.stage;
            QMetaObject::invokeMethod(
                this,
                [this, cancelled, progress] {
                    if (!discovering || cancelled != discovery_cancelled || cancelled->requested.load() ||
                        progress.percent < scan_progress.percent)
                        return;
                    scan_progress = progress;
                    emit discovery_progress_changed();
                },
                Qt::QueuedConnection);
        };
    auto *watcher = new QFutureWatcher<std::pair<QVector<app_record>, QVector<discovery_issue>>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, report_progress] {
        try {
            auto result = watcher->result();
            if (discovery_cancelled->requested.load()) {
                discovering = false;
                const auto message = discovery_summary(
                    0, result.second,
                    QCoreApplication::translate("XLibrary", "Scan cancelled; changes were not saved."));
                scan_report = discovery_details(message, result.second);
                emit discovery_report_changed();
                emit discovery_changed();
                notify(message);
                watcher->deleteLater();
                return;
            }
            scan_progress = {90, 0, result.first.size(),
                             QCoreApplication::translate("XLibrary", "Adding applications to the library")};
            emit discovery_progress_changed();
            enqueue("discover", [applications = std::move(result.first), issues = std::move(result.second),
                                 cancelled = discovery_cancelled, report_progress](CatalogWorker &worker) mutable {
                catalog_import_result count;
                try {
                    count = worker.store->import_records(
                        applications, cancelled.get(),
                        [&](qsizetype processed, qsizetype total) {
                            report_progress(
                                {total > 0 ? 90 + static_cast<int>(9.0 * processed / total) : 99, processed, total,
                                 QCoreApplication::translate("XLibrary", "Adding applications to the library")});
                        },
                        &issues);
                } catch (const DiscoveryCancelled &) {
                    const auto message = discovery_summary(
                        0, issues, QCoreApplication::translate("XLibrary", "Scan cancelled; changes were not saved."));
                    emit worker.discovery_report_ready(discovery_details(message, issues));
                    emit worker.notification(message);
                    throw;
                } catch (const std::exception &error) {
                    issues.append(discovery_issue{"SQLite", QString::fromUtf8(error.what())});
                    const auto message = discovery_summary(
                        0, issues, QCoreApplication::translate("XLibrary", "Scan failed; changes were not saved."));
                    emit worker.discovery_report_ready(discovery_details(message, issues));
                    emit worker.notification(message);
                    throw;
                }
                report_progress({99, applications.size(), applications.size(),
                                 QCoreApplication::translate("XLibrary", "Refreshing the library")});
                const auto message =
                    discovery_summary(count.added, issues, QCoreApplication::translate("XLibrary", "Scan complete."));
                emit worker.discovery_report_ready(discovery_details(message, issues));
                emit worker.notification(message);
                worker.refresh();
            });
        } catch (const std::exception &error) {
            const QVector<discovery_issue> issues{{"Discovery", QString::fromUtf8(error.what())}};
            const auto message = discovery_summary(
                0, issues, QCoreApplication::translate("XLibrary", "Scan failed; changes were not saved."));
            scan_report = discovery_details(message, issues);
            emit discovery_report_changed();
            notify(message);
            discovering = false;
            emit discovery_changed();
        }
        watcher->deleteLater();
    });
    watcher->setFuture(
        QtConcurrent::run(&discovery_pool, [registry, start_menu, cancelled = discovery_cancelled, report_progress] {
            QVector<discovery_issue> issues;
            auto applications =
                desktop_platform::discover(registry, start_menu, issues, cancelled->requested, report_progress);
            return std::make_pair(std::move(applications), std::move(issues));
        }));
}

void DesktopService::cancel_discovery() {
    if (!discovering)
        return;
    auto expected = discovery_phase::running;
    if (!discovery_cancelled->phase.compare_exchange_strong(expected, discovery_phase::cancelling))
        return;
    discovery_cancelled->requested.store(true);
    scan_progress.stage = QCoreApplication::translate("XLibrary", "Stopping the scan…");
    emit discovery_progress_changed();
    notify(QCoreApplication::translate("XLibrary", "Stopping application discovery…"));
}

void DesktopService::export_catalog(const QString &path) {
    enqueue("export", [path](CatalogWorker &worker) {
        const auto too_large = [] {
            return std::runtime_error(
                QCoreApplication::translate(
                    "XLibrary", "The JSON catalog exceeds 32 MiB or 50000 applications. Export was not saved. "
                                "Use a database backup for a larger library.")
                    .toStdString());
        };
        if (worker.catalog.applications.size() > catalog_limits::applications)
            throw too_large();
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            throw std::runtime_error(
                QCoreApplication::translate("XLibrary", "Could not open the export file.").toStdString());
        qint64 written = 0;
        const auto write = [&](const QByteArray &contents) {
            if (contents.size() > catalog_limits::json_bytes - written)
                throw too_large();
            if (file.write(contents) != contents.size())
                throw std::runtime_error(
                    QCoreApplication::translate("XLibrary", "Could not save the export.").toStdString());
            written += contents.size();
        };
        write("{\"format\":1,\"applications\":[");
        bool first = true;
        for (const auto &record : worker.catalog.applications) {
            try {
                CatalogStore::validate_application(record);
            } catch (const std::invalid_argument &error) {
                throw std::runtime_error(QCoreApplication::translate("XLibrary", "Cannot export %1: %2")
                                             .arg(record.path, QString::fromUtf8(error.what()))
                                             .toStdString());
            }
            const auto contents = QJsonDocument(QJsonObject{{"name", record.name},
                                                            {"path", record.path},
                                                            {"arguments", record.arguments},
                                                            {"working_directory", record.working_directory},
                                                            {"favorite", record.favorite},
                                                            {"tags", QJsonArray::fromStringList(record.tags)}})
                                      .toJson(QJsonDocument::Compact);
            if (!first)
                write(",");
            first = false;
            write(contents);
        }
        write("]}\n");
        if (!file.commit())
            throw std::runtime_error(
                QCoreApplication::translate("XLibrary", "Could not save the export.").toStdString());
        emit worker.notification(QCoreApplication::translate("XLibrary", "Library exported."));
    });
}

void DesktopService::import_catalog(const QString &path) {
    enqueue("import", [path](CatalogWorker &worker) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            throw std::runtime_error(
                QCoreApplication::translate("XLibrary", "Could not read the import file.").toStdString());
        const auto contents = file.read(catalog_limits::json_bytes + 1);
        if (contents.size() > catalog_limits::json_bytes)
            throw std::runtime_error(
                QCoreApplication::translate("XLibrary", "The JSON catalog exceeds 32 MiB. Import cancelled.")
                    .toStdString());
        if (file.error() != QFileDevice::NoError)
            throw std::runtime_error(file.errorString().toStdString());
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(contents, &error);
        if (error.error != QJsonParseError::NoError)
            throw std::runtime_error(
                QCoreApplication::translate("XLibrary", "JSON error at offset %1: %2. Import cancelled.")
                    .arg(error.offset)
                    .arg(error.errorString())
                    .toStdString());
        const auto root = document.object();
        if (!document.isObject())
            throw std::runtime_error(
                QCoreApplication::translate("XLibrary", "Field root: expected a JSON object. Import cancelled.")
                    .toStdString());
        if (!root.value("format").isDouble() || root.value("format").toDouble() != 1)
            throw std::runtime_error(
                QCoreApplication::translate("XLibrary", "Field format: only version 1 is supported. Import cancelled.")
                    .toStdString());
        if (!root.value("applications").isArray())
            throw std::runtime_error(
                QCoreApplication::translate("XLibrary", "Field applications: expected an array. Import cancelled.")
                    .toStdString());
        const auto applications = root.value("applications").toArray();
        if (applications.size() > catalog_limits::applications)
            throw std::runtime_error(QCoreApplication::translate(
                                         "XLibrary", "The JSON catalog exceeds 50000 applications. Import cancelled.")
                                         .toStdString());
        QVector<app_record> records;
        for (const auto &value : applications) {
            const auto fail = [&](const QString &field, const QString &reason) {
                throw std::runtime_error(
                    QCoreApplication::translate("XLibrary",
                                                "Record %1, field %2: %3. Import cancelled; changes were not saved.")
                        .arg(records.size() + 1)
                        .arg(field, reason)
                        .toStdString());
            };
            if (!value.isObject())
                fail("applications[]", QCoreApplication::translate("XLibrary", "expected an object"));
            const auto object = value.toObject();
            const auto text = [&](const QString &field, bool required = false) {
                const auto item = object.value(field);
                if ((!item.isUndefined() || required) && !item.isString())
                    fail(field, QCoreApplication::translate("XLibrary", "expected a string"));
                const auto result = item.toString();
                if (result.contains(QChar::Null))
                    fail(field, QCoreApplication::translate("XLibrary", "null characters are not allowed"));
                return result;
            };
            app_record record;
            record.name = text("name", true).trimmed();
            record.path = QDir::fromNativeSeparators(text("path", true));
            record.arguments = text("arguments");
            record.working_directory = text("working_directory");
            if (object.contains("favorite") && !object.value("favorite").isBool())
                fail("favorite", QCoreApplication::translate("XLibrary", "expected a boolean"));
            record.favorite = object.value("favorite").toBool();
            if (object.contains("tags") && !object.value("tags").isArray())
                fail("tags", QCoreApplication::translate("XLibrary", "expected an array of strings"));
            qsizetype tag_index = 0;
            for (const auto &tag : object.value("tags").toArray()) {
                const auto field = QString("tags[%1]").arg(++tag_index);
                if (!tag.isString())
                    fail(field, QCoreApplication::translate("XLibrary", "expected a string"));
                const auto name = tag.toString().trimmed();
                if (!valid_tag_name(name))
                    fail(field,
                         QCoreApplication::translate(
                             "XLibrary",
                             "a tag must contain 1 to 64 characters, without spaces, commas, #, or null characters"));
                record.tags.append(name);
            }
            if (record.tags.size() > catalog_limits::tags_per_application)
                fail("tags", QCoreApplication::translate("XLibrary", "at most 256 tags are allowed"));
            record.tags.removeDuplicates();
            if (record.name.isEmpty() || record.name.size() > 256)
                fail("name", QCoreApplication::translate("XLibrary", "the name must contain 1 to 256 characters"));
            if (!QFileInfo(record.path).isAbsolute())
                fail("path", QCoreApplication::translate("XLibrary", "expected an absolute path"));
#ifdef Q_OS_WIN
            if (QFileInfo(record.path).suffix().isEmpty())
                fail("path", QCoreApplication::translate("XLibrary", "a Windows target requires a filename extension"));
#endif
            if (!record.working_directory.isEmpty() && !QFileInfo(record.working_directory).isAbsolute())
                fail("working_directory",
                     QCoreApplication::translate("XLibrary", "expected an empty string or an absolute path"));
            try {
                CatalogStore::validate_application(record);
            } catch (const std::invalid_argument &error) {
                fail("application", QString::fromUtf8(error.what()));
            }
            records.append(std::move(record));
        }
        const auto count = worker.store->import_records(records);
        worker.refresh();
        emit worker.notification(QCoreApplication::translate("XLibrary", "Applications imported: %1. Duplicates: %2.")
                                     .arg(count.added)
                                     .arg(count.duplicates));
    });
}

void DesktopService::clear_history() {
    enqueue("history", [](CatalogWorker &worker) {
        worker.store->clear_history();
        worker.refresh();
        emit worker.notification(QCoreApplication::translate("XLibrary", "Launch history cleared."));
    });
}

void DesktopService::notify(const QString &text) {
    emit message(text);
}

#include "desktop_service.moc"
