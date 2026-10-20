#ifndef XLIBRARY_DATABASE_H
#define XLIBRARY_DATABASE_H

#include <filesystem>
#include <memory>
#include <sqlite3.h>
#include <vector>

#include "Application.h"

class Database {
private:
    bool drop_on_add_error = true;
    std::filesystem::path file_path;
    std::unique_ptr<sqlite3, decltype(&sqlite3_close_v2)> connection{nullptr, &sqlite3_close_v2};

    static std::filesystem::path get_database_path();
    void execute(const char *sql) const;
    void initialize() const;

public:
    Database();
    explicit Database(const bool drop_on_add_error) : Database() { this->drop_on_add_error = drop_on_add_error; }
    explicit Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;

    std::filesystem::path setup() const;
    std::filesystem::path get_file_path() const { return this->file_path; }

    bool add_application(const Application &application) const;
    bool add_application(const std::string &file_title, const std::filesystem::path &file_path) const;

    bool already_exists(const std::string &file_title) const;
    bool already_exists(const std::filesystem::path &file_path) const;

    sqlite3 *get_connection() const noexcept { return this->connection.get(); }

    std::vector<Application> get_applications() const;
};

#endif //XLIBRARY_DATABASE_H
