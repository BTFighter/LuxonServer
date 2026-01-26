#pragma once

#include "global.hpp"

#include <expected>
#include <functional>

struct mco_coro;

namespace minicoro {
// Exception type used internally for forced unwinding
// Only safe to catch right before exiting coroutine
struct ForcedUnwind {};

enum class State { Dead, Normal, Suspended, Running };

class Coroutine {
public:
    using Task = std::move_only_function<void(Coroutine&)>;

    Coroutine(Task task, size_t stack_size = 0, size_t storage_size = 1024);

    Coroutine(const Coroutine&) = delete;
    Coroutine& operator=(const Coroutine&) = delete;

    Coroutine(Coroutine&& other) noexcept;
    Coroutine& operator=(Coroutine&& other) noexcept;

    ~Coroutine();

    // Returns void on success, or an error code if the underlying C API fails.
    [[nodiscard]]
    std::expected<void, int> resume();

    void yield();

    [[nodiscard]]
    State status() const;

    static Coroutine *current();

private:
    mco_coro *handle_ = nullptr;
    Task task_;
    bool stop_requested_ = false;

    // Internal helpers to bridge C API
    static void entry_point_proxy(mco_coro *co);
};
} // namespace minicoro
