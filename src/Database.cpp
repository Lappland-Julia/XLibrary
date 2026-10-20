#include <cstdlib>
#include <stdexcept>
#include <string>

#include "Database.h"

std::filesystem::path Database::get_database_path() {
    const auto *local_app_data = _wgetenv(L"LOCALAPPDATA");
    if (local_app_data == nullptr || local_app_data[0] == L'\0') {
        throw std::runtime_error("LOCALAPPDATA is not set");
    }

    const std::filesystem::path local_app_data_path(local_app_data);
    if (!local_app_data_path.is_absolute()) {
        throw std::runtime_error("LOCALAPPDATA must be an absolute path");
    }

    return local_app_data_path / L"XLibrary" / L"xlibrary.db";
}

void Database::execute(const char *sql) const {
    if (sqlite3_exec(this->connection.get(), sql, nullptr, nullptr, nullptr) != SQLITE_OK) {
        throw std::runtime_error(std::string("Database initialization failed: ") +
                                 sqlite3_errmsg(this->connection.get()));
    }
}

void Database::initialize() const {
    this->execute("PRAGMA foreign_keys = ON;");
    this->execute(R"(
        BEGIN TRANSACTION;

        CREATE TABLE IF NOT EXISTS applications (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            path TEXT
        );

        CREATE TABLE IF NOT EXISTS tags (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL UNIQUE
        );

        CREATE TABLE IF NOT EXISTS application_tags (
            application_id INTEGER NOT NULL REFERENCES applications(id) ON DELETE CASCADE,
            tag_id INTEGER NOT NULL REFERENCES tags(id) ON DELETE CASCADE,
            PRIMARY KEY (application_id, tag_id)
        );

        CREATE INDEX IF NOT EXISTS application_tags_tag_id ON application_tags(tag_id);

        COMMIT;
    )");
}

Database::Database() : file_path(get_database_path()) {
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
    if (this->already_exists(file_title) || this->already_exists(file_path)) {
        if (this->drop_on_add_error) {
            throw std::invalid_argument(file_title + " app is already exists in database");
        }
        return false;
    }

    sqlite3_stmt *raw_statement = nullptr;
    const int result = sqlite3_prepare_v2(
        this->connection.get(), "INSERT INTO applications (name, path) VALUES (?, ?);", -1, &raw_statement, nullptr);
    const std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> statement(raw_statement, &sqlite3_finalize);
    if (result != SQLITE_OK) {
        throw std::runtime_error(std::string("Cannot add application: ") + sqlite3_errmsg(this->connection.get()));
    }

    const auto utf8_path = file_path.u8string();
    const std::string path_string(utf8_path.begin(), utf8_path.end());
    if (sqlite3_bind_text64(statement.get(), 1, file_title.data(), file_title.size(), SQLITE_TRANSIENT, SQLITE_UTF8) !=
            SQLITE_OK ||
        sqlite3_bind_text64(statement.get(), 2, path_string.data(), path_string.size(), SQLITE_TRANSIENT,
                            SQLITE_UTF8) != SQLITE_OK ||
        sqlite3_step(statement.get()) != SQLITE_DONE) {
        throw std::runtime_error(std::string("Cannot add application: ") + sqlite3_errmsg(this->connection.get()));
    }
    return true;
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
    sqlite3_stmt *raw_statement = nullptr;
    const int result = sqlite3_prepare_v2(this->connection.get(), "SELECT 1 FROM applications WHERE path = ? LIMIT 1;",
                                          -1, &raw_statement, nullptr);
    const std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> statement(raw_statement, &sqlite3_finalize);
    if (result != SQLITE_OK) {
        throw std::runtime_error(std::string("Cannot check application: ") + sqlite3_errmsg(this->connection.get()));
    }

    const auto utf8_path = file_path.u8string();
    const std::string path_string(utf8_path.begin(), utf8_path.end());
    if (sqlite3_bind_text64(statement.get(), 1, path_string.data(), path_string.size(), SQLITE_TRANSIENT,
                            SQLITE_UTF8) != SQLITE_OK) {
        throw std::runtime_error(std::string("Cannot check application: ") + sqlite3_errmsg(this->connection.get()));
    }

    const int step_result = sqlite3_step(statement.get());
    if (step_result != SQLITE_ROW && step_result != SQLITE_DONE) {
        throw std::runtime_error(std::string("Cannot check application: ") + sqlite3_errmsg(this->connection.get()));
    }

    return step_result == SQLITE_ROW;
}

std::vector<Application> Database::get_applications() const {
    std::vector<Application> applications{};

    sqlite3_stmt *raw_statement = nullptr;
    int result =
        sqlite3_prepare_v2(this->connection.get(), "SELECT name, path FROM applications;", -1, &raw_statement, nullptr);

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
