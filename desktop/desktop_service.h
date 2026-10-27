#ifndef XLIBRARY_DESKTOP_SERVICE_H
#define XLIBRARY_DESKTOP_SERVICE_H

#include <atomic>
#include <functional>
#include <QSettings>
#include <QThread>
#include <QTimer>

#include "library_model.h"
#include "update_checker.h"

class CatalogWorker;

class DesktopService : public QObject {
    Q_OBJECT
    Q_PROPERTY(qint64 selection READ selection NOTIFY selection_changed)
    Q_PROPERTY(QString section READ section NOTIFY navigation_changed)
    Q_PROPERTY(bool ready READ is_ready NOTIFY catalog_changed)
private:
    QThread worker_thread;
    CatalogWorker *worker;
    QThreadPool discovery_pool;
    QTimer scenario_timer;
    QVector<scenario_step> pending_steps;
    QString scenario_name;
    qsizetype step_index = 0;
    bool scenario_active = false;
    std::shared_ptr<std::atomic<quint64>> scenario_generation = std::make_shared<std::atomic<quint64>>(0);
    QSet<qint64> launching;
    bool discovering = false;
    discovery_progress scan_progress;
    QString scan_report;
    std::shared_ptr<discovery_control> discovery_cancelled = std::make_shared<discovery_control>();
    bool ready = false;
    qint64 selected_id = 0;
    QString current_section = "library";
    QString current_tag;
    catalog_snapshot snapshot;
    void enqueue(const QString &operation, std::function<void(CatalogWorker &)> task);
    void next_scenario_step();

public:
    LibraryModel model;
    LibraryFilter filter;
    QSettings settings;
    UpdateChecker updates;
    explicit DesktopService(QObject *parent = nullptr, const QString &profile_path = {},
                            const QString &database_path = {});
    ~DesktopService() override;
    bool is_ready() const { return ready; }
    bool is_discovering() const { return discovering; }
    bool is_cancelling_discovery() const {
        return discovering && discovery_cancelled->phase.load() == discovery_phase::cancelling;
    }
    bool can_cancel_discovery() const {
        return discovering && discovery_cancelled->phase.load() == discovery_phase::running;
    }
    const discovery_progress &discovery_status() const { return scan_progress; }
    QString discovery_report() const { return scan_report; }
    bool is_scenario_active() const { return scenario_active; }
    qint64 selection() const { return selected_id; }
    const app_record *selected() const { return model.find(selected_id); }
    const catalog_snapshot &catalog() const { return snapshot; }
    QString section() const { return current_section; }
    QString tag() const { return current_tag; }
    Q_INVOKABLE void select(qint64 id);
    Q_INVOKABLE void navigate(const QString &section, const QString &tag = {});
    Q_INVOKABLE void save_application(app_record record);
    Q_INVOKABLE void resolve_application(const QString &path);
    Q_INVOKABLE void remove_application(qint64 id);
    Q_INVOKABLE void save_tag(const QString &old_name, const QString &new_name);
    Q_INVOKABLE void remove_tag(const QString &name);
    Q_INVOKABLE void save_scenario(scenario_record record);
    Q_INVOKABLE void remove_scenario(qint64 id);
    Q_INVOKABLE void run_scenario(qint64 id);
    Q_INVOKABLE void cancel_scenario();
    Q_INVOKABLE void discover();
    Q_INVOKABLE void cancel_discovery();
    Q_INVOKABLE void export_catalog(const QString &path);
    Q_INVOKABLE void import_catalog(const QString &path);
    Q_INVOKABLE void launch(qint64 id, bool new_instance = false, bool scenario = false);
    Q_INVOKABLE void open_folder(qint64 id);
    void clear_history();
    void notify(const QString &message);

signals:
    void catalog_changed();
    void selection_changed();
    void navigation_changed();
    void message(const QString &text);
    void operation_finished(const QString &operation, bool success);
    void application_resolved(const QString &requested_path, const app_record &record);
    void scenario_saved(qint64 id);
    void discovery_changed();
    void discovery_progress_changed();
    void discovery_report_changed();
    void launch_succeeded();
    void scenario_changed(const QString &status);
    void appearance_changed();
};

#endif
