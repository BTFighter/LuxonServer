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
        plugins = new std::remove_reference_t<decltype(*plugins)>();
    return plugins->emplace(type, callback).second;
}

std::optional<AuthResult> call(unsigned int type, const std::string& requested_user_id, const std::string& params, const std::string& data,
                               const std::optional<std::string>& secret, const std::optional<std::string>& auth_url) {
    if (!plugins)
        return std::nullopt;

    auto res = plugins->find(type);
    if (res == plugins->end())
        return std::nullopt;

    return res->second(requested_user_id, params, data, secret, auth_url);
}
} // namespace registry
} // namespace auth_plugins
} // namespace server
