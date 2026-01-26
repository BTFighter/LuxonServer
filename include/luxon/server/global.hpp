#pragma once

#include <version>

namespace luxon {}

namespace server {
using namespace luxon;
}

#if !defined(__cpp_lib_move_only_function)
#include <memory>
#include <utility>
#include <type_traits>
#include <functional>

namespace std {
template <typename Signature> class move_only_function;

template <typename R, typename... Args> class move_only_function<R(Args...)> {
    struct Concept {
        virtual ~Concept() = default;
        virtual R call(Args&&...) = 0;
    };

    template <typename F> struct Model final : Concept {
        F f_;
        Model(F&& f) : f_(std::move(f)) {}
        R call(Args&&...args) override { return f_(std::forward<Args>(args)...); }
    };

    std::unique_ptr<Concept> self_;

public:
    move_only_function() = default;

    // Template constructor to accept lambdas/functors
    template <typename F, typename = std::enable_if_t<!std::is_same_v<std::decay_t<F>, move_only_function>>>
    move_only_function(F&& f) : self_(std::make_unique<Model<std::decay_t<F>>>(std::forward<F>(f))) {}

    // Movable but not copyable
    move_only_function(move_only_function&&) = default;
    move_only_function& operator=(move_only_function&&) = default;
    move_only_function(const move_only_function&) = delete;
    move_only_function& operator=(const move_only_function&) = delete;

    // Invocation
    R operator()(Args... args) const { return self_->call(std::forward<Args>(args)...); }

    explicit operator bool() const { return static_cast<bool>(self_); }
};
} // namespace std
#endif
