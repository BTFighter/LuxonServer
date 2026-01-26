#include "coroutine.hpp"

#define MCO_USE_VMEM_ALLOCATOR
#define MINICORO_IMPL
#include "minicoro.h"

#include <string>
#include <exception>
#include <stdexcept>

#if defined(__cpp_lib_unreachable)
#include <utility>
#define TRAP() std::unreachable()
#elif defined(_MSC_VER)
#define TRAP() __debugbreak()
#else
#define TRAP() __builtin_trap()
#endif

namespace minicoro {
Coroutine::Coroutine(Task task, size_t stack_size, size_t storage_size) : task_(std::move(task)) {
    mco_desc desc = mco_desc_init(Coroutine::entry_point_proxy, stack_size);
    desc.user_data = this;
    desc.storage_size = storage_size;

    mco_result res = mco_create(&handle_, &desc);
    if (res != MCO_SUCCESS)
        throw std::runtime_error("Failed to create coroutine: " + std::string(mco_result_description(res)));
}

Coroutine::Coroutine(Coroutine&& other) noexcept : handle_(other.handle_), task_(std::move(other.task_)), stop_requested_(other.stop_requested_) {
    other.handle_ = nullptr;
    if (handle_)
        handle_->user_data = this;
}

Coroutine& Coroutine::operator=(Coroutine&& other) noexcept {
    if (this != &other) {
        // Destroy current coroutine properly before reassigning
        if (handle_)
            this->~Coroutine();
        handle_ = other.handle_;
        task_ = std::move(other.task_);
        stop_requested_ = other.stop_requested_;
        other.handle_ = nullptr;
        if (handle_)
            handle_->user_data = this;
    }
    return *this;
}

Coroutine::~Coroutine() {
    if (!handle_)
        return;

    // Destroying a coroutine from within itself is ILLEGAL
    if (current() == this)
        TRAP();

    // We must wake coroutine up one last time to destroy it properly
    if (mco_status(handle_) == MCO_SUSPENDED) {
        stop_requested_ = true;
        mco_resume(handle_);
    }

    mco_destroy(handle_);
    handle_ = nullptr;
}

std::expected<void, int> Coroutine::resume() {
    if (!handle_)
        return std::unexpected(-1);

    mco_result res = mco_resume(handle_);

    if (res != MCO_SUCCESS)
        return std::unexpected(static_cast<int>(res));
    return {};
}

void Coroutine::yield() {
    if (!handle_)
        return;

    mco_yield(handle_);

    // Check if we need to die
    if (stop_requested_)
        throw ForcedUnwind{};
}

State Coroutine::status() const {
    if (!handle_)
        return State::Dead;

    switch (mco_status(handle_)) {
    case MCO_DEAD:
        return State::Dead;
    case MCO_NORMAL:
        return State::Normal;
    case MCO_SUSPENDED:
        return State::Suspended;
    case MCO_RUNNING:
        return State::Running;
    default:
        return State::Dead;
    }
}

Coroutine *Coroutine::current() {
    if (mco_coro *co = mco_running())
        return static_cast<Coroutine *>(mco_get_user_data(co));

    return nullptr;
}

void Coroutine::entry_point_proxy(mco_coro *co) {
    // Retrieve the C++ object
    auto *self = static_cast<Coroutine *>(mco_get_user_data(co));

    try {
        if (self && self->task_)
            // Execute the user's C++ task
            self->task_(*self);
    } catch (const ForcedUnwind&) {
        // Catch this one specifically to finish C++ stack unwinding
    } catch (...) {
        // Do not let exceptions escape into C land
        std::terminate();
    }
    // Function exits and status becomes MCO_DEAD
}
} // namespace minicoro
