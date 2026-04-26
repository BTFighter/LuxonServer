// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "global.hpp"

#include <string>
#include <expected>
#include <optional>
#include <luxon/ser_types.hpp>

namespace server {
namespace auth_plugins {
namespace registry {
using AuthResult = std::expected<std::string, ser::OperationResponseMessage>;
using AuthCallback = AuthResult (*)(std::string_view, std::string_view, std::string_view);

bool register_(unsigned type, AuthCallback callback);
std::optional<AuthResult> call(unsigned type, std::string_view requested_user_id, std::string_view params, std::string_view data);
} // namespace registry
} // namespace auth_plugins
} // namespace server

// Macro magic
#ifdef _MSC_VER
#define LN_REGISTER_AUTH_PLUGIN(name, type, callback)                                                                                                          \
    class name##__initializer {                                                                                                                                \
    public:                                                                                                                                                    \
        name##__initializer() { ::server::auth_plugins::registry::register_(type, callback); }                                                                 \
    } name##__initializer_instance;

#else
#define LN_REGISTER_AUTH_PLUGIN(name, type, callback)                                                                                                          \
    __attribute__((constructor(65530))) __attribute__((used)) __attribute__((retain)) static void register_auth_plugin_##name() {                              \
        ::server::auth_plugins::registry::register_(type, callback);                                                                                           \
    }

#endif
