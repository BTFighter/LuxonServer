// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

#include "game_plugin_registry.hpp"

#include <unordered_map>

namespace server {
namespace game_plugins {
namespace registry {
namespace {
std::unordered_map<std::string, PluginFactory> *plugins{};
}

bool register_(const std::string& name, PluginFactory&& plugin_factory) {
    if (!plugins)
        plugins = new std::remove_reference_t<decltype(*plugins)>();
    return plugins->emplace(name, std::move(plugin_factory)).second;
}

std::unique_ptr<PluginBase> instantiate(Game *game, const std::string& name) {
    if (!plugins)
        return nullptr;

    auto res = plugins->find(name);
    if (res == plugins->end())
        return nullptr;

    return res->second(game);
}
} // namespace registry
} // namespace game_plugins
} // namespace server
