#include <algorithm>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <stdexcept>

#include "catalog_store.h"
#include "localization.h"
#include "path_helpers.h"

namespace {
void validate_application(const app_record &record) {
    if (record.name.trimmed().isEmpty() || record.name.size() > 256 || !QFileInfo(record.path).isAbsolute() ||
        record.name.contains(QChar::Null) || record.path.contains(QChar::Null) ||
        record.arguments.contains(QChar::Null) || record.working_directory.contains(QChar::Null)) {
        throw std::invalid_argument(
            QCoreApplication::translate("XLibrary", "Invalid application name or path.").toStdString());
    }
    if (!record.working_directory.isEmpty() && !QFileInfo(record.working_directory).isAbsolute()) {
        throw std::invalid_argument(
            QCoreApplication::translate("XLibrary", "The working directory must be an absolute path.").toStdString());
    }
    for (const auto &tag : record.tags) {
        if (!valid_tag_name(tag))
            throw std::invalid_argument(
                QCoreApplication::translate("XLibrary", "Tag: up to 64 characters, without spaces, commas, or #.")
                    .toStdString());
    }
}

class Statement {
    sqlite3 *connection;
    std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> statement{nullptr, &sqlite3_finalize};

public:
    Statement(sqlite3 *connection, const char *sql) : connection(connection) {
        sqlite3_stmt *raw = nullptr;
        const auto result = sqlite3_prepare_v2(connection, sql, -1, &raw, nullptr);
        statement.reset(raw);
        if (result != SQLITE_OK) {
            throw std::runtime_error(sqlite3_errmsg(connection));
        }
    }
    void bind(int index, const QString &value) {
        const auto utf8 = value.toUtf8();
        if (sqlite3_bind_text64(statement.get(), index, utf8.constData(), utf8.size(), SQLITE_TRANSIENT, SQLITE_UTF8) !=
            SQLITE_OK) {
            throw std::runtime_error(sqlite3_errmsg(connection));
        }
    }
    void bind(int index, qint64 value) {
        if (sqlite3_bind_int64(statement.get(), index, value) != SQLITE_OK) {
            throw std::runtime_error(sqlite3_errmsg(connection));
        }
    }
    bool next() {
        const auto result = sqlite3_step(statement.get());
        if (result != SQLITE_ROW && result != SQLITE_DONE) {
            throw std::runtime_error(sqlite3_errmsg(connection));
        }
        return result == SQLITE_ROW;
    }
    QString text(int column) const {
        const auto *value = sqlite3_column_text(statement.get(), column);
        return value ? QString::fromUtf8(reinterpret_cast<const char *>(value),
                                         sqlite3_column_bytes(statement.get(), column))
                     : QString{};
    }
    qint64 number(int column) const { return sqlite3_column_int64(statement.get(), column); }
};

class Transaction {
    sqlite3 *connection;
    bool committed = false;

public:
    explicit Transaction(sqlite3 *connection) : connection(connection) {
        if (sqlite3_exec(connection, "SAVEPOINT desktop_write;", nullptr, nullptr, nullptr) != SQLITE_OK) {
            throw std::runtime_error(sqlite3_errmsg(connection));
        }
    }
    ~Transaction() {
        if (!committed) {
            sqlite3_exec(connection, "ROLLBACK TO desktop_write; RELEASE desktop_write;", nullptr, nullptr, nullptr);
        }
    }
    void commit() {
        if (sqlite3_exec(connection, "RELEASE desktop_write;", nullptr, nullptr, nullptr) != SQLITE_OK) {
            throw std::runtime_error(sqlite3_errmsg(connection));
        }
        committed = true;
    }
};
}

void CatalogStore::execute(const char *sql) const {
    if (sqlite3_exec(database.get_connection(), sql, nullptr, nullptr, nullptr) != SQLITE_OK) {
        throw std::runtime_error(sqlite3_errmsg(database.get_connection()));
    }
}

CatalogStore::CatalogStore(const QString &database_path) : database(filesystem_path(database_path), false) {
    auto *connection = database.get_connection();
    sqlite3_busy_timeout(connection, 1500);
    bool has_desktop = false;
    {
        Statement schema(connection, "SELECT 1 FROM sqlite_master WHERE name = 'desktop_application';");
        has_desktop = schema.next();
    }
    if (!has_desktop) {
        // SQLite backup accounts for the journal without modifying the source tables.
        auto backup_path = database.get_file_path();
        backup_path += ".before-desktop.bak";
        if (std::filesystem::exists(backup_path)) {
            backup_path += "." + std::to_string(QDateTime::currentMSecsSinceEpoch());
        }
        auto temporary_path = backup_path;
        temporary_path += ".partial";
        {
            sqlite3 *raw = nullptr;
            const auto utf8 = std::filesystem::path(temporary_path).u8string();
            const auto result = sqlite3_open(reinterpret_cast<const char *>(utf8.c_str()), &raw);
            const std::unique_ptr<sqlite3, decltype(&sqlite3_close_v2)> destination(raw, &sqlite3_close_v2);
            if (result != SQLITE_OK) {
                throw std::runtime_error("Cannot create database backup");
            }
            auto *backup = sqlite3_backup_init(destination.get(), "main", connection, "main");
            if (!backup) {
                throw std::runtime_error("Cannot initialize database backup");
            }
            const auto copied = sqlite3_backup_step(backup, -1);
            const auto finished = sqlite3_backup_finish(backup);
            if (copied != SQLITE_DONE || finished != SQLITE_OK) {
                throw std::runtime_error("Cannot finish database backup");
            }
        }
        std::filesystem::rename(temporary_path, backup_path);
    }
    Transaction transaction(connection);
    execute(R"(
        CREATE INDEX IF NOT EXISTS desktop_applications_name ON applications(name);
        CREATE INDEX IF NOT EXISTS desktop_applications_path ON applications(path);
        CREATE TABLE IF NOT EXISTS desktop_application (
            application_id INTEGER PRIMARY KEY REFERENCES applications(id) ON DELETE CASCADE,
            arguments TEXT NOT NULL DEFAULT '', working_directory TEXT NOT NULL DEFAULT '',
            favorite INTEGER NOT NULL DEFAULT 0, last_launch INTEGER NOT NULL DEFAULT 0
        );
        CREATE TABLE IF NOT EXISTS desktop_scenario (
            id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL
        );
        CREATE TABLE IF NOT EXISTS desktop_scenario_step (
            scenario_id INTEGER NOT NULL REFERENCES desktop_scenario(id) ON DELETE CASCADE,
            position INTEGER NOT NULL, application_id INTEGER NOT NULL REFERENCES applications(id) ON DELETE CASCADE,
            delay_ms INTEGER NOT NULL CHECK(delay_ms BETWEEN 0 AND 3600000), enabled INTEGER NOT NULL,
            PRIMARY KEY(scenario_id, position)
        );
    )");
    transaction.commit();
}

catalog_snapshot CatalogStore::read() const {
    catalog_snapshot result;
    auto *connection = database.get_connection();
    Statement applications(connection, R"(
        SELECT a.id, a.name, a.path, coalesce(d.arguments,''), coalesce(d.working_directory,''),
               coalesce(d.favorite,0), coalesce(d.last_launch,0)
        FROM applications a LEFT JOIN desktop_application d ON a.id = d.application_id ORDER BY a.id;
    )");
    QHash<qint64, qsizetype> rows;
    while (applications.next()) {
        app_record record;
        record.id = applications.number(0);
        record.name = applications.text(1);
        record.path = QDir::fromNativeSeparators(applications.text(2));
        record.arguments = applications.text(3);
        record.working_directory = applications.text(4);
        record.favorite = applications.number(5) != 0;
        record.last_launch = applications.number(6);
        record.available = QFileInfo(record.path).isFile();
        rows.insert(record.id, result.applications.size());
        result.applications.append(std::move(record));
    }
    Statement tags(connection, "SELECT name FROM tags ORDER BY name COLLATE NOCASE;");
    while (tags.next()) {
        result.tags.append(tags.text(0));
    }
    Statement links(
        connection,
        "SELECT at.application_id, t.name FROM application_tags at JOIN tags t ON t.id = at.tag_id ORDER BY t.name;");
    while (links.next()) {
        const auto row = rows.constFind(links.number(0));
        if (row != rows.cend()) {
            result.applications[*row].tags.append(links.text(1));
        }
    }
    Statement scenarios(connection, "SELECT id, name FROM desktop_scenario ORDER BY name;");
    while (scenarios.next()) {
        scenario_record record{scenarios.number(0), scenarios.text(1), {}};
        Statement steps(connection, "SELECT application_id, delay_ms, enabled FROM desktop_scenario_step WHERE "
                                    "scenario_id = ? ORDER BY position;");
        steps.bind(1, record.id);
        while (steps.next()) {
            record.steps.append({steps.number(0), static_cast<int>(steps.number(1)), steps.number(2) != 0});
        }
        result.scenarios.append(std::move(record));
    }
    return result;
}

void CatalogStore::write_metadata(qint64 id, const app_record &record) {
    auto *connection = database.get_connection();
    Statement metadata(connection, R"(
        INSERT INTO desktop_application(application_id, arguments, working_directory, favorite) VALUES(?,?,?,?)
        ON CONFLICT(application_id) DO UPDATE SET arguments=excluded.arguments,
        working_directory=excluded.working_directory, favorite=excluded.favorite;
    )");
    metadata.bind(1, id);
    metadata.bind(2, record.arguments);
    metadata.bind(3, record.working_directory);
    metadata.bind(4, record.favorite);
    metadata.next();
    Statement clear(connection, "DELETE FROM application_tags WHERE application_id = ?;");
    clear.bind(1, id);
    clear.next();
    QHash<QString, QString> known_tags;
    Statement existing_tags(connection, "SELECT name FROM tags;");
    while (existing_tags.next()) {
        const auto name = existing_tags.text(0);
        known_tags.insert(name.toCaseFolded(), name);
    }
    for (const auto &tag : record.tags) {
        const auto canonical_tag = known_tags.value(tag.toCaseFolded(), tag);
        Statement create(connection, "INSERT OR IGNORE INTO tags(name) VALUES(?);");
        create.bind(1, canonical_tag);
        create.next();
        Statement link(connection, "INSERT OR IGNORE INTO application_tags SELECT ?, id FROM tags WHERE name = ?;");
        link.bind(1, id);
        link.bind(2, canonical_tag);
        link.next();
        known_tags.insert(tag.toCaseFolded(), canonical_tag);
    }
}

qint64 CatalogStore::save_application(const app_record &record) {
    validate_application(record);
    auto *connection = database.get_connection();
    Transaction transaction(connection);
    qint64 id = record.id;
    Statement duplicate(connection, "SELECT id, name, path FROM applications WHERE id != ?;");
    duplicate.bind(1, id);
    while (duplicate.next()) {
        if (duplicate.text(1) == record.name || normalized_path(duplicate.text(2)) == normalized_path(record.path)) {
            throw std::runtime_error(QCoreApplication::translate(
                                         "XLibrary", "An application with this name or path is already in the library.")
                                         .toStdString());
        }
    }
    if (id == 0) {
        const auto name = record.name.toUtf8();
        if (!database.add_application(std::string(name.constData(), name.size()), filesystem_path(record.path)))
            throw std::runtime_error(
                QCoreApplication::translate("XLibrary", "The application is already in the library.").toStdString());
        id = sqlite3_last_insert_rowid(connection);
    } else {
        Statement update(connection, "UPDATE applications SET name = ?, path = ? WHERE id = ?;");
        update.bind(1, record.name);
        update.bind(2, record.path);
        update.bind(3, id);
        update.next();
        if (sqlite3_changes(connection) == 0) {
            throw std::runtime_error(
                QCoreApplication::translate("XLibrary", "The application has already been removed from the library.")
                    .toStdString());
        }
    }
    write_metadata(id, record);
    transaction.commit();
    return id;
}

void CatalogStore::remove_application(qint64 id) {
    Statement statement(database.get_connection(), "DELETE FROM applications WHERE id = ?;");
    statement.bind(1, id);
    statement.next();
}

void CatalogStore::save_tag(const QString &old_name, const QString &new_name) {
    if (!valid_tag_name(new_name))
        throw std::invalid_argument(QCoreApplication::translate("XLibrary", "Invalid tag name.").toStdString());
    Transaction transaction(database.get_connection());
    Statement existing(database.get_connection(), "SELECT name FROM tags;");
    while (existing.next()) {
        if (existing.text(0) != old_name && existing.text(0).compare(new_name, Qt::CaseInsensitive) == 0) {
            throw std::invalid_argument(
                QCoreApplication::translate("XLibrary", "A tag with this name already exists.").toStdString());
        }
    }
    Statement statement(database.get_connection(), old_name.isEmpty() ? "INSERT INTO tags(name) VALUES(?);"
                                                                      : "UPDATE tags SET name = ? WHERE name = ?;");
    statement.bind(1, new_name);
    if (!old_name.isEmpty()) {
        statement.bind(2, old_name);
    }
    statement.next();
    transaction.commit();
}

void CatalogStore::remove_tag(const QString &name) {
    Statement statement(database.get_connection(), "DELETE FROM tags WHERE name = ?;");
    statement.bind(1, name);
    statement.next();
}

qint64 CatalogStore::save_scenario(const scenario_record &record) {
    if (record.name.trimmed().isEmpty() || record.name.size() > 128 || record.steps.isEmpty() ||
        record.steps.size() > 1000) {
        throw std::invalid_argument(
            QCoreApplication::translate("XLibrary", "Enter a scenario name and between 1 and 1000 steps.")
                .toStdString());
    }
    auto *connection = database.get_connection();
    Transaction transaction(connection);
    auto id = record.id;
    if (id == 0) {
        Statement insert(connection, "INSERT INTO desktop_scenario(name) VALUES(?);");
        insert.bind(1, record.name);
        insert.next();
        id = sqlite3_last_insert_rowid(connection);
    } else {
        Statement update(connection, "UPDATE desktop_scenario SET name = ? WHERE id = ?;");
        update.bind(1, record.name);
        update.bind(2, id);
        update.next();
        Statement clear(connection, "DELETE FROM desktop_scenario_step WHERE scenario_id = ?;");
        clear.bind(1, id);
        clear.next();
    }
    for (qsizetype index = 0; index < record.steps.size(); ++index) {
        const auto &step = record.steps[index];
        Statement insert(connection, "INSERT INTO desktop_scenario_step VALUES(?,?,?,?,?);");
        insert.bind(1, id);
        insert.bind(2, index);
        insert.bind(3, step.application_id);
        insert.bind(4, step.delay_ms);
        insert.bind(5, step.enabled);
        insert.next();
    }
    transaction.commit();
    return id;
}

void CatalogStore::remove_scenario(qint64 id) {
    Statement statement(database.get_connection(), "DELETE FROM desktop_scenario WHERE id = ?;");
    statement.bind(1, id);
    statement.next();
}

void CatalogStore::record_launch(qint64 id) {
    Statement statement(database.get_connection(),
                        "INSERT INTO desktop_application(application_id,last_launch) VALUES(?,?) ON "
                        "CONFLICT(application_id) DO UPDATE SET last_launch=excluded.last_launch;");
    statement.bind(1, id);
    statement.bind(2, QDateTime::currentSecsSinceEpoch());
    statement.next();
}

QString CatalogStore::path() const {
    return path_text(database.get_file_path());
}

void CatalogStore::clear_history() {
    execute("UPDATE desktop_application SET last_launch = 0;");
}

catalog_import_result CatalogStore::import_records(const QVector<app_record> &records, discovery_control *control,
                                                   const std::function<void(qsizetype, qsizetype)> &progress,
                                                   QVector<discovery_issue> *issues) {
    QVector<const app_record *> candidates;
    candidates.reserve(records.size());
    for (const auto &record : records) {
        if (control && control->phase.load() == discovery_phase::cancelling)
            throw DiscoveryCancelled();
        try {
            validate_application(record);
            candidates.append(&record);
        } catch (const std::invalid_argument &error) {
            if (!issues)
                throw;
            issues->append(discovery_issue{record.path, QString::fromUtf8(error.what())});
        }
    }
    Transaction transaction(database.get_connection());
    QSet<QString> names;
    QSet<QString> paths;
    Statement current(database.get_connection(), "SELECT name, path FROM applications;");
    while (current.next()) {
        names.insert(current.text(0));
        paths.insert(normalized_path(current.text(1)));
    }
    catalog_import_result result;
    for (qsizetype index = 0; index < candidates.size(); ++index) {
        if (control && control->phase.load() == discovery_phase::cancelling)
            throw DiscoveryCancelled();
        if (progress)
            progress(index, candidates.size());
        const auto &record = *candidates[index];
        const auto path = normalized_path(record.path);
        if (names.contains(record.name) || paths.contains(path)) {
            ++result.duplicates;
            if (issues)
                issues->append(discovery_issue{
                    record.path,
                    names.contains(record.name)
                        ? QCoreApplication::translate("XLibrary",
                                                      "The library already contains an entry with this name.")
                        : QCoreApplication::translate("XLibrary",
                                                      "The library already contains an entry with this path."),
                    discovery_issue_kind::duplicate});
        } else {
            if (!database.add_application(record.name.toStdString(), filesystem_path(record.path))) {
                ++result.duplicates;
                if (issues)
                    issues->append(discovery_issue{
                        record.path,
                        QCoreApplication::translate("XLibrary", "The entry already exists in the database."),
                        discovery_issue_kind::duplicate});
                continue;
            }
            const auto id = sqlite3_last_insert_rowid(database.get_connection());
            write_metadata(id, record);
            names.insert(record.name);
            paths.insert(path);
            ++result.added;
        }
    }
    if (control) {
        // The only cancellation boundary: a request before this point rolls back the entire transaction.
        auto expected = discovery_phase::running;
        if (!control->phase.compare_exchange_strong(expected, discovery_phase::committing))
            throw DiscoveryCancelled();
    }
    transaction.commit();
    if (progress)
        progress(candidates.size(), candidates.size());
    return result;
}
