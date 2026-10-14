#ifndef XLIBRARY_HEARTBEAT_H
#define XLIBRARY_HEARTBEAT_H

#include <cstdint>
#include <filesystem>
#include <memory>
#include <thread>

class Heartbeat {
private:
    struct State;
    std::shared_ptr<State> state;
    std::jthread worker;

public:
    Heartbeat(std::uint32_t process_id, const std::filesystem::path &executable_path);
    Heartbeat(const Heartbeat &) = delete;
    Heartbeat &operator=(const Heartbeat &) = delete;
    Heartbeat(Heartbeat &&) noexcept = default;
    Heartbeat &operator=(Heartbeat &&) noexcept = default;

    Heartbeat() {};

    bool is_alive() const noexcept;
    std::uint32_t get_error_code() const noexcept;
    void request_stop() noexcept;
};

#endif //XLIBRARY_HEARTBEAT_H
