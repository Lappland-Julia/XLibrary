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
#include "windows_integration.h"

class CatalogWorker : public QObject {
    Q_OBJECT
public:
    std::unique_ptr<CatalogStore> store;
    catalog_snapshot catalog;
    QTimer *monitor = nullptr;
    QSet<QString> last_running;
    bool monitor_error = false;

    void initialize() {
        try {
            store = std::make_unique<CatalogStore>();
            refresh();
            monitor = new QTimer(this);
            monitor->setInterval(2500);
            connect(monitor, &QTimer::timeout, this, [this] { poll(); });
            monitor->start();
        } catch (const std::exception &error) {
            emit failed(QString("Не удалось открыть библиотеку: %1").arg(QString::fromUtf8(error.what())));
        }
    }
    void refresh() {
        catalog = store->read();
        for (auto &record : catalog.applications)
            record.running = last_running.contains(QDir::fromNativeSeparators(record.path).toCaseFolded());
        emit loaded(catalog);
        poll();
    }
    void poll() {
        try {
            const auto paths = desktop_windows::running_paths(catalog.applications);
            monitor_error = false;
            if (paths != last_running) {
                last_running = paths;
                emit running_changed(paths);
            }
        } catch (const std::exception &error) {
            if (!monitor_error)
                emit failed(QString::fromUtf8(error.what()));
            monitor_error = true;
        }
    }

signals:
    void loaded(const catalog_snapshot &catalog);
    void running_changed(const QSet<QString> &paths);
    void failed(const QString &message);
    void completed(const QString &operation, bool success);
    void resolved(const QString &requested_path, const app_record &record);
    void scenario_saved(qint64 id);
    void tag_renamed(const QString &old_name, const QString &new_name);
    void launched(qint64 id, bool success, quint64 scenario);
    void notification(const QString &message);
};

DesktopService::DesktopService(QObject *parent, const QString &profile_path)
    : QObject(parent), worker(new CatalogWorker), model(this), filter(this),
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
    connect(worker, &CatalogWorker::failed, this, &DesktopService::message);
    connect(worker, &CatalogWorker::notification, this, &DesktopService::message);
    connect(worker, &CatalogWorker::completed, this, &DesktopService::operation_finished);
    connect(this, &DesktopService::operation_finished, this, [this](const QString &operation, bool success) {
        if (operation == "discover") {
            if (success) {
                scan_progress.percent = 100;
                scan_progress.stage = "Сканирование завершено";
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
        model.replace(value.applications);
        if (!model.find(selected_id))
            selected_id = 0;
        emit catalog_changed();
        emit selection_changed();
    });
    connect(worker, &CatalogWorker::running_changed, this, [this](const QSet<QString> &paths) {
        model.update_running(paths);
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
            emit scenario_changed("Сценарий остановлен из-за ошибки запуска.");
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
    discovery_cancelled->store(true);
    scenario_timer.stop();
    discovery_pool.clear();
    discovery_pool.waitForDone();
    worker_thread.quit();
    worker_thread.wait();
}

void DesktopService::enqueue(const QString &operation, std::function<void(CatalogWorker &)> task) {
    QMetaObject::invokeMethod(
        worker,
        [worker = worker, operation, task = std::move(task)] {
            try {
                if (!worker->store)
                    throw std::runtime_error(
                        "Библиотека недоступна. Перезапустите приложение после устранения ошибки.");
                task(*worker);
                emit worker->completed(operation, true);
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
            [path](CatalogWorker &worker) { emit worker.resolved(path, desktop_windows::resolve_application(path)); });
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
            notify("Приложение ещё запускается. Повторите сценарий после завершения запуска.");
        }
        return;
    }
    launching.insert(id);
    const auto generation = scenario ? scenario_generation->load() : 0;
    const bool activate = !new_instance && settings.value("behavior/relaunch", "activate").toString() == "activate";
    enqueue("launch", [id, activate, generation, token = scenario_generation](CatalogWorker &worker) {
        try {
            if (generation && token->load() != generation) {
                emit worker.launched(id, false, generation);
                return;
            }
            const auto it = std::find_if(worker.catalog.applications.cbegin(), worker.catalog.applications.cend(),
                                         [id](const auto &record) { return record.id == id; });
            if (it == worker.catalog.applications.cend())
                throw std::runtime_error("Приложение не найдено в библиотеке.");
            if (!activate || !desktop_windows::activate_window(it->path)) {
                desktop_windows::launch_application(*it);
            }
            // Ошибка истории не должна превращать успешный запуск в неудачный или повторный.
            emit worker.launched(id, true, generation);
            try {
                worker.store->record_launch(id);
                worker.refresh();
            } catch (const std::exception &error) {
                emit worker.failed(
                    QString("Приложение открыто, но история не сохранена: %1").arg(QString::fromUtf8(error.what())));
            }
        } catch (...) {
            emit worker.launched(id, false, generation);
            throw;
        }
    });
}

void DesktopService::open_folder(qint64 id) {
    if (const auto *record = model.find(id))
        enqueue("folder", [path = record->path](CatalogWorker &) { desktop_windows::open_folder(path); });
}

void DesktopService::run_scenario(qint64 id) {
    if (scenario_active) {
        notify("Уже выполняется сценарий. Сначала остановите его.");
        return;
    }
    for (const auto &record : snapshot.scenarios) {
        if (record.id == id) {
            pending_steps.clear();
            for (const auto &step : record.steps)
                if (step.enabled)
                    pending_steps.append(step);
            if (pending_steps.isEmpty()) {
                notify("В сценарии нет включённых приложений.");
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
        emit scenario_changed("Сценарий «" + scenario_name + "» выполнен.");
        return;
    }
    emit scenario_changed(
        QString("%1 · шаг %2 из %3").arg(scenario_name).arg(step_index + 1).arg(pending_steps.size()));
    scenario_timer.start(pending_steps[step_index].delay_ms);
}

void DesktopService::cancel_scenario() {
    ++*scenario_generation;
    scenario_active = false;
    scenario_timer.stop();
    emit scenario_changed("Сценарий остановлен. Уже открытые приложения продолжают работать.");
}

void DesktopService::discover() {
    if (discovering || !ready)
        return;
    const bool registry = settings.value("sources/registry", true).toBool();
    const bool start_menu = settings.value("sources/start_menu", true).toBool();
    if (!registry && !start_menu) {
        notify("Выберите источники в настройках библиотеки.");
        return;
    }
    discovering = true;
    discovery_cancelled = std::make_shared<std::atomic_bool>(false);
    scan_progress = {-1, 0, 0, "Подготовка списка приложений…"};
    emit discovery_changed();
    // Обновления из обоих рабочих потоков приходят в UI не чаще смены процента или этапа.
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
                    if (!discovering || cancelled != discovery_cancelled || cancelled->load() ||
                        progress.percent < scan_progress.percent)
                        return;
                    scan_progress = progress;
                    emit discovery_progress_changed();
                },
                Qt::QueuedConnection);
        };
    auto *watcher = new QFutureWatcher<std::pair<QVector<app_record>, QStringList>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, report_progress] {
        try {
            if (discovery_cancelled->load()) {
                discovering = false;
                emit discovery_changed();
                notify("Поиск приложений отменён.");
                watcher->deleteLater();
                return;
            }
            auto result = watcher->result();
            scan_progress = {90, 0, result.first.size(), "Добавление приложений в библиотеку"};
            emit discovery_progress_changed();
            enqueue("discover", [applications = std::move(result.first), cancelled = discovery_cancelled,
                                 report_progress](CatalogWorker &worker) {
                const auto count = worker.store->import_records(
                    applications, cancelled.get(), [&](qsizetype processed, qsizetype total) {
                        report_progress({total > 0 ? 90 + static_cast<int>(9.0 * processed / total) : 99, processed,
                                         total, "Добавление приложений в библиотеку"});
                    });
                report_progress({99, applications.size(), applications.size(), "Обновление библиотеки"});
                worker.refresh();
                emit worker.notification(QString("Добавлено приложений: %1").arg(count));
            });
        } catch (const std::exception &error) {
            notify(QString::fromUtf8(error.what()));
            discovering = false;
            emit discovery_changed();
        }
        watcher->deleteLater();
    });
    watcher->setFuture(
        QtConcurrent::run(&discovery_pool, [registry, start_menu, cancelled = discovery_cancelled, report_progress] {
            QStringList issues;
            auto applications = desktop_windows::discover(registry, start_menu, issues, *cancelled, report_progress);
            return std::make_pair(std::move(applications), std::move(issues));
        }));
}

void DesktopService::cancel_discovery() {
    if (!discovering || discovery_cancelled->load())
        return;
    discovery_cancelled->store(true);
    scan_progress.stage = "Остановка сканирования…";
    emit discovery_progress_changed();
    notify("Остановка поиска приложений…");
}

void DesktopService::export_catalog(const QString &path) {
    enqueue("export", [path](CatalogWorker &worker) {
        QJsonArray applications;
        for (const auto &record : worker.catalog.applications) {
            applications.append(QJsonObject{{"name", record.name},
                                            {"path", record.path},
                                            {"arguments", record.arguments},
                                            {"working_directory", record.working_directory},
                                            {"favorite", record.favorite},
                                            {"tags", QJsonArray::fromStringList(record.tags)}});
        }
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            throw std::runtime_error("Не удалось открыть файл экспорта.");
        const auto contents = QJsonDocument(QJsonObject{{"format", 1}, {"applications", applications}}).toJson();
        if (file.write(contents) != contents.size() || !file.commit())
            throw std::runtime_error("Не удалось сохранить экспорт.");
        emit worker.notification("Библиотека экспортирована.");
    });
}

void DesktopService::import_catalog(const QString &path) {
    enqueue("import", [path](CatalogWorker &worker) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || file.size() > 5 * 1024 * 1024)
            throw std::runtime_error("Невозможно прочитать файл или его размер превышает 5 МБ.");
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(file.readAll(), &error);
        const auto root = document.object();
        if (error.error != QJsonParseError::NoError || root.value("format").toInt() != 1 ||
            !root.value("applications").isArray())
            throw std::runtime_error("Неподдерживаемый формат библиотеки.");
        QVector<app_record> records;
        for (const auto &value : root.value("applications").toArray()) {
            const auto object = value.toObject();
            app_record record;
            record.name = object.value("name").toString().trimmed();
            record.path = QDir::fromNativeSeparators(object.value("path").toString());
            record.arguments = object.value("arguments").toString();
            record.working_directory = object.value("working_directory").toString();
            record.favorite = object.value("favorite").toBool();
            for (const auto &tag : object.value("tags").toArray())
                if (!tag.toString().trimmed().isEmpty())
                    record.tags.append(tag.toString().trimmed());
            if (record.name.isEmpty() || !QFileInfo(record.path).isAbsolute() ||
                QFileInfo(record.path).suffix().isEmpty())
                throw std::runtime_error("В файле есть запись без имени или абсолютного пути.");
            records.append(std::move(record));
        }
        const int count = worker.store->import_records(records);
        worker.refresh();
        emit worker.notification(QString("Импортировано приложений: %1. Дубли пропущены.").arg(count));
    });
}

void DesktopService::clear_history() {
    enqueue("history", [](CatalogWorker &worker) {
        worker.store->clear_history();
        worker.refresh();
        emit worker.notification("История запусков очищена.");
    });
}

void DesktopService::notify(const QString &text) {
    emit message(text);
}

#include "desktop_service.moc"
