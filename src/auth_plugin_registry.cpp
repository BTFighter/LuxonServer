// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

#include "auth_plugin_registry.hpp"

#include <unordered_map>

namespace server {
namespace auth_plugins {
namespace registry {
namespace {
std::unordered_map<unsigned, AuthCallback> *plugins{};
}

bool register_(unsigned type, AuthCallback callback) {
    if (!plugins)
        plugins = new typeof(*plugins);
    return plugins->emplace(type, callback).second;
}

std::optional<AuthResult> call(unsigned int type, std::string_view requested_user_id, std::string_view params, std::string_view data) {
    if (!plugins)
        return std::nullopt;

    auto res = plugins->find(type);
    if (res == plugins->end())
        return std::nullopt;

    return res->second(requested_user_id, params, data);
}
} // namespace registry
} // namespace auth_plugins
} // namespace server
