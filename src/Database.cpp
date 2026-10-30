#include <stdexcept>
#include <string>

#include "Database.h"
#include "launch_identity.h"
#include "profile_schema.h"
#include "sqlite_helpers.h"

void Database::execute(const char *sql) const {
    if (sqlite3_exec(this->connection.get(), sql, nullptr, nullptr, nullptr) != SQLITE_OK) {
        throw std::runtime_error(std::string("Database initialization failed: ") +
                                 sqlite3_errmsg(this->connection.get()));
    }
}

void Database::initialize() const {
    this->execute("PRAGMA foreign_keys = ON;");
    xlibrary::migrate_profile(this->connection.get(), this->file_path);
}

Database::Database() : Database({}, true) {
}

Database::Database(const std::filesystem::path &path, bool drop_on_add_error)
    : drop_on_add_error(drop_on_add_error), file_path(path.empty() ? get_database_path() : path) {
    if (!this->file_path.is_absolute())
        throw std::invalid_argument("Database path must be absolute");
    std::filesystem::create_directories(this->file_path.parent_path());

    const auto utf8_path = this->file_path.u8string();
    const std::string path_string(utf8_path.begin(), utf8_path.end());
    sqlite3 *database_connection = nullptr;
    const int result =
        sqlite3_open_v2(path_string.c_str(), &database_connection, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    this->connection.reset(database_connection);

    if (result != SQLITE_OK) {
        const auto *error_message =
            database_connection != nullptr ? sqlite3_errmsg(database_connection) : sqlite3_errstr(result);
        throw std::runtime_error(std::string("Cannot open database: ") + error_message);
    }

    sqlite3_busy_timeout(this->connection.get(), 1500);
    this->initialize();
}

std::filesystem::path Database::setup() const {
    this->initialize();
    return this->get_file_path();
}

bool Database::add_application(const Application &application) const {
    return this->add_application(application.get_file_title(), application.get_file_path());
}

bool Database::add_application(const std::string &file_title, const std::filesystem::path &file_path) const {
    auto *connection = this->connection.get();
    xlibrary::sqlite_execute(connection, "SAVEPOINT backend_application;");
    try {
        const auto configuration_id = xlibrary::ensure_launch_configuration(connection, file_path);
        xlibrary::SqliteStatement existing(connection, "SELECT 1 FROM applications WHERE configuration_id=? LIMIT 1;");
        existing.bind(1, configuration_id);
        if (existing.next()) {
            if (this->drop_on_add_error)
                throw std::invalid_argument("This launch configuration already has a library card");
            xlibrary::sqlite_execute(connection, "RELEASE backend_application;");
            return false;
        }
        xlibrary::SqliteStatement insert(connection, "INSERT INTO applications(name,configuration_id) VALUES(?,?);");
        insert.bind(1, file_title);
        insert.bind(2, configuration_id);
        insert.next();
        xlibrary::sqlite_execute(connection, "RELEASE backend_application;");
        return true;
    } catch (...) {
        sqlite3_exec(connection, "ROLLBACK TO backend_application; RELEASE backend_application;", nullptr, nullptr,
                     nullptr);
        throw;
    }
}

bool Database::already_exists(const std::string &file_title) const {
    sqlite3_stmt *raw_statement = nullptr;
    const int result = sqlite3_prepare_v2(this->connection.get(), "SELECT 1 FROM applications WHERE name = ? LIMIT 1;",
                                          -1, &raw_statement, nullptr);
    const std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> statement(raw_statement, &sqlite3_finalize);
    if (result != SQLITE_OK || sqlite3_bind_text64(statement.get(), 1, file_title.data(), file_title.size(),
                                                   SQLITE_TRANSIENT, SQLITE_UTF8) != SQLITE_OK) {
        throw std::runtime_error(std::string("Cannot check application: ") + sqlite3_errmsg(this->connection.get()));
    }

    const int step_result = sqlite3_step(statement.get());
    if (step_result != SQLITE_ROW && step_result != SQLITE_DONE) {
        throw std::runtime_error(std::string("Cannot check application: ") + sqlite3_errmsg(this->connection.get()));
    }

    return step_result == SQLITE_ROW;
}

bool Database::already_exists(const std::filesystem::path &file_path) const {
    const auto identity = xlibrary::path_identity(file_path);
    xlibrary::SqliteStatement statement(
        this->connection.get(),
        "SELECT t.identity_key FROM applications a JOIN launch_configuration c ON c.id=a.configuration_id "
        "JOIN launch_target t ON t.id=c.target_id;");
    while (statement.next()) {
        if (statement.text(0) == identity)
            return true;
    }
    return false;
}

std::vector<Application> Database::get_applications() const {
    std::vector<Application> applications{};

    sqlite3_stmt *raw_statement = nullptr;
    int result = sqlite3_prepare_v2(
        this->connection.get(),
        "SELECT a.name,c.path FROM applications a JOIN launch_configuration c ON c.id=a.configuration_id;", -1,
        &raw_statement, nullptr);

    const std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> statement(raw_statement, &sqlite3_finalize);

    if (result != SQLITE_OK) {
        throw std::runtime_error(sqlite3_errmsg(this->connection.get()));
    }

    while ((result = sqlite3_step(statement.get())) == SQLITE_ROW) {
        const bool path_is_null = sqlite3_column_type(statement.get(), 1) == SQLITE_NULL;
        const auto *name = sqlite3_column_text(statement.get(), 0);
        const auto *path = sqlite3_column_text(statement.get(), 1);

        if (name == nullptr || (path == nullptr && !path_is_null)) {
            throw std::runtime_error("Cannot read application data");
        }

        const std::string file_title(reinterpret_cast<const char *>(name), sqlite3_column_bytes(statement.get(), 0));
        std::filesystem::path file_path;
        if (!path_is_null) {
            const std::u8string path_string(reinterpret_cast<const char8_t *>(path),
                                            sqlite3_column_bytes(statement.get(), 1));
            file_path = path_string;
        }

        auto app = Application(file_title, file_path);
        applications.push_back(app);
    }

    if (result != SQLITE_DONE) {
        throw std::runtime_error(sqlite3_errmsg(this->connection.get()));
    }

    return applications;
}
