#ifndef XLIBRARY_APPLICATION_H
#define XLIBRARY_APPLICATION_H

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <string>



class Application {
private:
    std::string uuid = generate_uuid();
    std::string file_title = "";
    std::filesystem::path file_path = LR"(~)";
    static std::string generate_uuid();
    static std::string read_application_name(const std::filesystem::path &executable_path);
    void set_empty() { this->file_title=""; this->file_path=""; }

public:
    std::string get_uuid() const { return this->uuid; }
    std::string get_file_title() const { return this->file_title; }
    std::filesystem::path get_file_path() const { return this->file_path; }

    void set_executable(std::filesystem::path const &file_path);
    std::uint32_t launch() const;

    Application();
    explicit Application(const std::string &file_title, const std::filesystem::path &executable_path);
    explicit Application(const std::filesystem::path &executable_path);

    friend bool operator==(const Application &a, const Application &b) {
        return (a.file_path == b.file_path && a.file_title == b.file_title);
    }
};

std::ostream& operator<<(std::ostream& stream, const Application& application);

#endif //XLIBRARY_APPLICATION_H
