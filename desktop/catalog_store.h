#ifndef XLIBRARY_CATALOG_STORE_H
#define XLIBRARY_CATALOG_STORE_H

#include <atomic>
#include <functional>

#include "catalog_types.h"
#include "Database.h"

class CatalogStore {
private:
    Database database;
    void execute(const char *sql) const;
    void write_metadata(qint64 id, const app_record &record);

public:
    explicit CatalogStore(const QString &database_path = {});
    static void validate_application(const app_record &record);
    static QString restore_backup(const QString &source, const QString &destination);
    catalog_snapshot read() const;
    qint64 save_application(const app_record &record);
    void remove_application(qint64 id);
    void save_tag(const QString &old_name, const QString &new_name);
    void remove_tag(const QString &name);
    qint64 save_scenario(const scenario_record &record);
    void remove_scenario(qint64 id);
    void record_launch(qint64 id);
    void clear_history();
    catalog_import_result import_records(const QVector<app_record> &records, discovery_control *control = nullptr,
                                         const std::function<void(qsizetype, qsizetype)> &progress = {},
                                         QVector<discovery_issue> *issues = nullptr);
    QString path() const;
};

#endif
