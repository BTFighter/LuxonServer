#include "logger.hpp"

#ifdef LUXON_SERVER_USE_SPDLOG
#include <spdlog/sinks/stdout_color_sinks.h>
#else
#include <string_view>
#include <iostream>
#include <print>
#endif

namespace server {

#ifdef LUXON_SERVER_USE_SPDLOG
std::shared_ptr<server::logger> create_logger(const std::string& name) {
#ifdef LUXON_SERVER_ENABLE_PLUGINS
    return spdlog::stdout_color_mt(name);
#else
    return spdlog::stdout_color_st(name);
#endif
}
#else
void logger::log_raw(log_level level, std::string message) {
    std::string_view level_str;
    switch (level) {
    case log_level::trace:
        level_str = "trace";
        break;
    case log_level::debug:
        level_str = "debug";
        break;
    case log_level::info:
        level_str = "info";
        break;
    case log_level::warn:
        level_str = "warning";
        break;
    case log_level::err:
        level_str = "error";
        break;
    case log_level::critical:
        level_str = "critical";
        break;
    default:
        return;
    }

    std::println("[{}] [{}] {}", name_, level_str, message);
    std::flush(std::cout);
}

std::shared_ptr<logger> create_logger(const std::string& name) { return std::make_shared<logger>(name); }
#endif
} // namespace server
