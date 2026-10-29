#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <iterator>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>

#include "linux_process.h"

namespace {
bool process_disappeared(const std::error_code &error) {
    return error == std::errc::no_such_file_or_directory || error == std::errc::no_such_process;
}

std::string read_process_file(const std::filesystem::path &path, std::error_code &error) {
    error.clear();
    errno = 0;
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        error = std::error_code(errno != 0 ? errno : EIO, std::generic_category());
        return {};
    }
    std::string contents{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    if (stream.bad()) {
        error = std::make_error_code(std::errc::io_error);
    }
    return contents;
}

std::string utf8_arguments(std::wstring_view arguments) {
    std::string result;
    for (const auto character : arguments) {
        const auto code = static_cast<std::uint32_t>(character);
        if (code == 0 || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) {
            throw std::invalid_argument("Invalid Unicode in application arguments");
        }
        if (code <= 0x7f) {
            result.push_back(static_cast<char>(code));
        } else if (code <= 0x7ff) {
            result.push_back(static_cast<char>(0xc0 | (code >> 6)));
            result.push_back(static_cast<char>(0x80 | (code & 0x3f)));
        } else if (code <= 0xffff) {
            result.push_back(static_cast<char>(0xe0 | (code >> 12)));
            result.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
            result.push_back(static_cast<char>(0x80 | (code & 0x3f)));
        } else {
            result.push_back(static_cast<char>(0xf0 | (code >> 18)));
            result.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3f)));
            result.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
            result.push_back(static_cast<char>(0x80 | (code & 0x3f)));
        }
    }
    return result;
}

std::vector<std::string> split_arguments(std::string_view arguments) {
    std::vector<std::string> result;
    std::string argument;
    char quote = 0;
    bool started = false;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const char character = arguments[index];
        if (character == '\\' && quote != '\'') {
            if (++index == arguments.size()) {
                throw std::invalid_argument("Application arguments end with an escape");
            }
            const char escaped = arguments[index];
            if (quote == '"' && escaped != '"' && escaped != '\\' && escaped != '$' && escaped != '`' &&
                escaped != '\n') {
                argument.push_back('\\');
            }
            if (escaped != '\n') {
                argument.push_back(escaped);
                started = true;
            }
        } else if (quote != 0) {
            if (character == quote) {
                quote = 0;
            } else {
                argument.push_back(character);
            }
        } else if (character == '\'' || character == '"') {
            quote = character;
            started = true;
        } else if (character == ' ' || character == '\t' || character == '\n' || character == '\r') {
            if (started) {
                result.push_back(std::move(argument));
                argument.clear();
                started = false;
            }
        } else {
            argument.push_back(character);
            started = true;
        }
    }
    if (quote != 0) {
        throw std::invalid_argument("Application arguments contain an unterminated quote");
    }
    if (started) {
        result.push_back(std::move(argument));
    }
    return result;
}

// Wait only for our own children: a global waitpid(-1) would reap Qt processes.
class ChildReaper {
    std::mutex mutex;
    std::condition_variable_any wake;
    std::vector<pid_t> children;
    std::jthread worker;

public:
    ChildReaper()
        : worker([this](std::stop_token stop_token) {
              std::unique_lock lock(mutex);
              while (!stop_token.stop_requested()) {
                  wake.wait(lock, stop_token, [this] { return !children.empty(); });
                  if (stop_token.stop_requested()) {
                      break;
                  }
                  std::erase_if(children, [](pid_t process_id) {
                      pid_t result;
                      do {
                          result = waitpid(process_id, nullptr, WNOHANG);
                      } while (result == -1 && errno == EINTR);
                      return result == process_id || (result == -1 && errno == ECHILD);
                  });
                  wake.wait_for(lock, stop_token, std::chrono::milliseconds(250), [] { return false; });
              }
          }) {}

    std::unique_lock<std::mutex> prepare() {
        std::unique_lock lock(mutex);
        children.reserve(children.size() + 1);
        return lock;
    }

    void watch(pid_t process_id) {
        children.push_back(process_id);
        wake.notify_all();
    }
};

class LaunchPipe {
public:
    int descriptors[2]{-1, -1};
    LaunchPipe() {
        if (pipe2(descriptors, O_CLOEXEC) == -1) {
            throw std::system_error(errno, std::generic_category(), "Cannot create application launch pipe");
        }
    }
    ~LaunchPipe() {
        for (const auto descriptor : descriptors) {
            if (descriptor != -1) {
                close(descriptor);
            }
        }
    }
};

bool script_matches(std::uint32_t process_id, const std::filesystem::path &expected_path) {
    std::ifstream script(expected_path, std::ios::binary);
    char prefix[2]{};
    if (!script.read(prefix, sizeof(prefix)) || prefix[0] != '#' || prefix[1] != '!') {
        return false;
    }
    std::string shebang;
    std::getline(script, shebang);
    std::istringstream interpreter_line(shebang);
    std::string interpreter;
    interpreter_line >> interpreter;

    std::error_code error;
    const auto command_line =
        read_process_file(std::filesystem::path("/proc") / std::to_string(process_id) / "cmdline", error);
    std::vector<std::string> arguments;
    for (std::size_t offset = 0; !error && offset < command_line.size();) {
        const auto end = command_line.find('\0', offset);
        if (end == std::string::npos) {
            return false;
        }
        arguments.push_back(command_line.substr(offset, end - offset));
        offset = end + 1;
    }
    if (arguments.size() < 2) {
        return false;
    }
    if (std::filesystem::path(interpreter).filename() != "env" &&
        std::filesystem::path(arguments.front()).filename() != std::filesystem::path(interpreter).filename()) {
        return false;
    }
    for (std::size_t index = 1; index < arguments.size(); ++index) {
        auto argument_path = std::filesystem::path(arguments[index]);
        if (argument_path.is_relative() && !argument_path.empty() && arguments[index].front() != '-') {
            const auto directory = std::filesystem::read_symlink(
                std::filesystem::path("/proc") / std::to_string(process_id) / "cwd", error);
            if (error) {
                return false;
            }
            argument_path = directory / argument_path;
        }
        if (argument_path == expected_path || std::filesystem::equivalent(argument_path, expected_path, error)) {
            return true;
        }
        if (arguments[index].empty() || arguments[index].front() != '-') {
            break;
        }
    }
    return false;
}
} // namespace

std::optional<xlibrary::process_identity> xlibrary::read_process_identity(std::uint32_t process_id,
                                                                          std::error_code &error) {
    const auto contents =
        read_process_file(std::filesystem::path("/proc") / std::to_string(process_id) / "stat", error);
    if (process_disappeared(error)) {
        error.clear();
        return std::nullopt;
    }
    if (error) {
        return std::nullopt;
    }
    // comm is enclosed in parentheses and may itself contain spaces, parentheses, and newlines.
    const auto name_end = contents.rfind(')');
    if (name_end == std::string::npos) {
        error = std::make_error_code(std::errc::io_error);
        return std::nullopt;
    }
    std::istringstream fields(contents.substr(name_end + 1));
    char state;
    std::string ignored;
    fields >> state;
    for (int field = 4; field < 22; ++field) {
        fields >> ignored;
    }
    process_identity identity{process_id, 0};
    if (!(fields >> identity.start_time) || identity.start_time == 0) {
        error = std::make_error_code(std::errc::io_error);
        return std::nullopt;
    }
    if (state == 'Z' || state == 'X' || state == 'x') {
        return std::nullopt;
    }
    return identity;
}

bool xlibrary::is_process_alive(const process_identity &identity, std::error_code &error) {
    const auto current = read_process_identity(identity.pid, error);
    return current && current->start_time == identity.start_time;
}

bool xlibrary::process_matches(const process_identity &identity, const std::filesystem::path &executable_path,
                               std::error_code &error) {
    if (!is_process_alive(identity, error)) {
        return false;
    }
    auto process_path =
        std::filesystem::read_symlink(std::filesystem::path("/proc") / std::to_string(identity.pid) / "exe", error);
    if (process_disappeared(error)) {
        error.clear();
        return false;
    }
    if (error) {
        return false;
    }
    if (process_path != executable_path && process_path.native().ends_with(" (deleted)")) {
        auto path_string = process_path.native();
        path_string.resize(path_string.size() - std::string_view(" (deleted)").size());
        process_path = std::move(path_string);
    }
    bool matches = process_path == executable_path;
    if (!matches) {
        matches = script_matches(identity.pid, executable_path);
    }
    return matches && is_process_alive(identity, error);
}

std::optional<xlibrary::process_identity> xlibrary::find_process(const std::filesystem::path &executable_path,
                                                                 std::error_code &error) {
    error.clear();
    std::filesystem::directory_iterator iterator("/proc", error), end;
    while (!error && iterator != end) {
        const auto name = iterator->path().filename().native();
        std::uint32_t process_id = 0;
        const auto conversion = std::from_chars(name.data(), name.data() + name.size(), process_id);
        if (conversion.ec == std::errc{} && conversion.ptr == name.data() + name.size() && process_id != 0) {
            std::error_code candidate_error;
            const auto identity = read_process_identity(process_id, candidate_error);
            if (identity && process_matches(*identity, executable_path, candidate_error)) {
                return identity;
            }
            // Other processes may be inaccessible due to ptrace/hidepid; this is not an application error.
        }
        iterator.increment(error);
    }
    return std::nullopt;
}

xlibrary::process_identity xlibrary::launch_process(const std::filesystem::path &executable_path,
                                                    const std::wstring &arguments,
                                                    const std::filesystem::path &working_directory) {
    if (executable_path.native().find('\0') != std::string::npos ||
        working_directory.native().find('\0') != std::string::npos) {
        throw std::invalid_argument("Application paths contain a null character");
    }
    auto argument_strings = split_arguments(utf8_arguments(arguments));
    argument_strings.insert(argument_strings.begin(), executable_path.native());
    std::vector<char *> argument_pointers;
    argument_pointers.reserve(argument_strings.size() + 1);
    for (auto &argument : argument_strings) {
        argument_pointers.push_back(argument.data());
    }
    argument_pointers.push_back(nullptr);
    // Perform all memory allocations and locking before fork.
    const auto *executable = executable_path.c_str();
    const auto *directory = working_directory.empty() ? nullptr : working_directory.c_str();
    auto *const *argv = argument_pointers.data();
    static ChildReaper reaper;
    LaunchPipe launch_pipe;
    auto reaper_lock = reaper.prepare();
    const auto process_id = fork();
    if (process_id == -1) {
        throw std::system_error(errno, std::generic_category(), "Cannot fork application process");
    }
    if (process_id == 0) {
        close(launch_pipe.descriptors[0]);
        if (directory == nullptr || chdir(directory) == 0) {
            execv(executable, argv);
        }
        const int launch_error = errno;
        const auto *bytes = reinterpret_cast<const char *>(&launch_error);
        std::size_t written = 0;
        while (written < sizeof(launch_error)) {
            const auto result = write(launch_pipe.descriptors[1], bytes + written, sizeof(launch_error) - written);
            if (result > 0) {
                written += static_cast<std::size_t>(result);
            } else if (result == 0 || errno != EINTR) {
                break;
            }
        }
        _exit(127);
    }
    reaper.watch(process_id);
    // Prevent the reaper from releasing the exited child's PID until starttime has been read.
    close(launch_pipe.descriptors[1]);
    launch_pipe.descriptors[1] = -1;
    int launch_error = 0;
    std::size_t received = 0;
    auto *bytes = reinterpret_cast<char *>(&launch_error);
    while (received < sizeof(launch_error)) {
        const auto result = read(launch_pipe.descriptors[0], bytes + received, sizeof(launch_error) - received);
        if (result > 0) {
            received += static_cast<std::size_t>(result);
        } else if (result == 0) {
            break;
        } else if (errno != EINTR) {
            throw std::system_error(errno, std::generic_category(), "Cannot read application launch result");
        }
    }
    if (received != 0) {
        throw std::system_error(received == sizeof(launch_error) ? launch_error : EIO, std::generic_category(),
                                "Application launch failed");
    }
    std::error_code error;
    const auto pid = static_cast<std::uint32_t>(process_id);
    return read_process_identity(pid, error).value_or(process_identity{pid, 0});
}
