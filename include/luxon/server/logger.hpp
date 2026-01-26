#pragma once

#include <string>
#include <memory>
#ifdef LUXON_SERVER_USE_SPDLOG
#include <spdlog/spdlog.h>
#else
#include <format>
#endif

namespace server {
#ifdef LUXON_SERVER_USE_SPDLOG
using log_level = spdlog::level::level_enum;
using logger = spdlog::logger;
#else
enum class log_level { trace, debug, info, warn, err, critical, off };

class logger {
    std::string name_;
    log_level level_ = log_level::debug;

    void log_raw(log_level level, std::string message);

public:
    logger(const std::string& name) : name_(name) {}

    void set_level(log_level level) { level_ = level; }

    template <typename... Args> void log(log_level level, std::format_string<Args...> fmt, Args&&...args) {
        if (static_cast<int>(level) >= static_cast<int>(level_))
            log_raw(level, std::format(fmt, std::forward<Args>(args)...));
    }

    template <typename... Args> void trace(std::format_string<Args...> fmt, Args&&...args) { log(log_level::trace, fmt, std::forward<Args>(args)...); }
    template <typename... Args> void debug(std::format_string<Args...> fmt, Args&&...args) { log(log_level::debug, fmt, std::forward<Args>(args)...); }
    template <typename... Args> void info(std::format_string<Args...> fmt, Args&&...args) { log(log_level::info, fmt, std::forward<Args>(args)...); }
    template <typename... Args> void warn(std::format_string<Args...> fmt, Args&&...args) { log(log_level::warn, fmt, std::forward<Args>(args)...); }
    template <typename... Args> void error(std::format_string<Args...> fmt, Args&&...args) { log(log_level::err, fmt, std::forward<Args>(args)...); }
    template <typename... Args> void critical(std::format_string<Args...> fmt, Args&&...args) { log(log_level::critical, fmt, std::forward<Args>(args)...); }
};
#endif

std::shared_ptr<logger> create_logger(const std::string& name);
} // namespace server
