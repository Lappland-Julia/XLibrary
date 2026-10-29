#ifndef XLIBRARY_APPLICATION_H
#define XLIBRARY_APPLICATION_H

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <memory>
#include <string>

class Application {
private:
    std::uint32_t pid = 0;
    // Copies share a Windows handle or a Linux PID/starttime pair that guards against PID reuse.
    std::shared_ptr<void> process_handle;
    std::string file_title = "";
    std::filesystem::path file_path = "~";
    static std::string read_application_name(const std::filesystem::path &executable_path);
    void set_empty() {
        this->file_title = "";
        this->file_path = "";
    }

public:
    std::uint32_t get_pid() const noexcept;
    std::string get_file_title() const { return this->file_title; }
    std::filesystem::path get_file_path() const { return this->file_path; }

    void set_executable(std::filesystem::path const &file_path);
    // On Windows, return 0 if Shell opened the application without an available new process PID.
    std::uint32_t launch();
    std::uint32_t launch(const std::wstring &arguments, const std::filesystem::path &working_directory);

    Application();
    explicit Application(const std::string &file_title, const std::filesystem::path &executable_path);
    explicit Application(const std::filesystem::path &executable_path);

    friend bool operator==(const Application &a, const Application &b) {
        return (a.file_path == b.file_path && a.file_title == b.file_title);
    }
};

std::ostream &operator<<(std::ostream &stream, const Application &application);

#endif //XLIBRARY_APPLICATION_H
