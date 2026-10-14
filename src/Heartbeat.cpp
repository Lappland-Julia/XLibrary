#include "Heartbeat.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <windows.h>
#include <tlhelp32.h>

namespace {
using unique_handle = std::unique_ptr<void, decltype(&CloseHandle)>;

std::pair<bool, DWORD> check_process_handle(HANDLE process_handle, const std::wstring &expected_path) {
    const auto wait_result = WaitForSingleObject(process_handle, 0);
    if (wait_result == WAIT_OBJECT_0) {
        return {false, 0};
    }
    if (wait_result == WAIT_FAILED) {
        return {false, GetLastError()};
    }

    wchar_t process_path[32768]{};
    DWORD path_length = static_cast<DWORD>(std::size(process_path));
    if (!QueryFullProcessImageNameW(process_handle, 0, process_path, &path_length)) {
        const auto error_code = GetLastError();
        if (WaitForSingleObject(process_handle, 0) == WAIT_OBJECT_0) {
            return {false, 0};
        }
        return {false, error_code};
    }

    const int comparison = CompareStringOrdinal(process_path, -1, expected_path.c_str(), -1, TRUE);
    if (comparison == 0) {
        return {false, GetLastError()};
    }
    // Процесс мог завершиться во время чтения пути.
    const auto final_wait = WaitForSingleObject(process_handle, 0);
    if (final_wait == WAIT_FAILED) {
        return {false, GetLastError()};
    }
    return {comparison == CSTR_EQUAL && final_wait == WAIT_TIMEOUT, 0};
}
}

struct Heartbeat::State {
    std::atomic<bool> alive{false};
    std::atomic<std::uint32_t> error_code{0};
};

Heartbeat::Heartbeat(std::uint32_t process_id, const std::filesystem::path &executable_path)
    : state(std::make_shared<State>()) {
    const auto expected_name = executable_path.filename().wstring();
    if (process_id == 0 || expected_name.empty()) {
        throw std::invalid_argument("Heartbeat requires a nonzero PID and an executable name");
    }
    const auto expected_path = std::filesystem::absolute(executable_path).lexically_normal().wstring();

    unique_handle process_handle(
        OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id),
        &CloseHandle);
    if (!process_handle) {
        const auto error_code = GetLastError();
        if (error_code != ERROR_INVALID_PARAMETER) {
            throw std::system_error(error_code, std::system_category(), "Cannot open heartbeat process");
        }
    }

    auto check_process = [process_handle = std::move(process_handle), expected_name, expected_path]() mutable {
        if (process_handle) {
            const auto result = check_process_handle(process_handle.get(), expected_path);
            if (result.first || result.second != 0) {
                return result;
            }
            process_handle.reset();
        }

        // Firefox и другие приложения могут передать запуск уже работающему экземпляру.
        const auto snapshot_handle = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot_handle == INVALID_HANDLE_VALUE) {
            return std::pair<bool, DWORD>{false, GetLastError()};
        }
        const unique_handle snapshot(snapshot_handle, &CloseHandle);
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        if (!Process32FirstW(snapshot.get(), &entry)) {
            const auto error_code = GetLastError();
            return std::pair<bool, DWORD>{false, error_code == ERROR_NO_MORE_FILES ? 0 : error_code};
        }

        DWORD check_error = 0;
        do {
            const int comparison = CompareStringOrdinal(entry.szExeFile, -1, expected_name.c_str(), -1, TRUE);
            if (comparison == 0) {
                return std::pair<bool, DWORD>{false, GetLastError()};
            }
            if (comparison != CSTR_EQUAL) {
                continue;
            }

            unique_handle candidate(
                OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID),
                &CloseHandle);
            if (!candidate) {
                const auto error_code = GetLastError();
                if (error_code != ERROR_INVALID_PARAMETER) {
                    check_error = error_code;
                }
                continue;
            }

            const auto [alive, error_code] = check_process_handle(candidate.get(), expected_path);
            if (alive) {
                // Удерживаем проверенный дескриптор, чтобы повторное использование PID его не подменило.
                process_handle = std::move(candidate);
                return std::pair<bool, DWORD>{true, 0};
            }
            if (error_code != 0) {
                check_error = error_code;
            }
        } while (Process32NextW(snapshot.get(), &entry));

        const auto enumeration_error = GetLastError();
        if (enumeration_error != ERROR_NO_MORE_FILES) {
            return std::pair<bool, DWORD>{false, enumeration_error};
        }
        return std::pair<bool, DWORD>{false, check_error};
    };

    const auto [alive, error_code] = check_process();
    if (error_code != 0) {
        throw std::system_error(error_code, std::system_category(), "Cannot check heartbeat process");
    }
    state->alive.store(alive);
    if (!alive) {
        return;
    }

    worker = std::jthread([state = state, check_process = std::move(check_process)](std::stop_token stop_token) mutable {
        std::mutex wait_mutex;
        std::condition_variable_any wake;
        std::unique_lock wait_lock(wait_mutex);
        while (!stop_token.stop_requested()) {
            wake.wait_for(wait_lock, stop_token, std::chrono::milliseconds(1500), [] { return false; });
            if (stop_token.stop_requested()) {
                break;
            }
            const auto [alive, error_code] = check_process();
            state->error_code.store(error_code);
            state->alive.store(alive);
            if (!alive) {
                break;
            }
        }
    });
}

bool Heartbeat::is_alive() const noexcept {
    return state && state->alive.load();
}

std::uint32_t Heartbeat::get_error_code() const noexcept {
    return state ? state->error_code.load() : 0;
}

void Heartbeat::request_stop() noexcept {
    worker.request_stop();
}
