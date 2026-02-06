// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "global.hpp"
#include "game_plugin_base.hpp"

#include <iostream>
#include <string>
#include <memory>
#include <functional>

namespace server {
namespace game_plugins {
namespace registry {
using PluginFactory = std::move_only_function<std::unique_ptr<PluginBase>(Game *)>;

bool register_(const std::string& name, PluginFactory&& plugin_factory);
std::unique_ptr<PluginBase> instanciate(Game *game, const std::string& name);

} // namespace registry
} // namespace game_plugins
} // namespace server

// Macro magic
#ifdef _MSC_VER
#define LN_REGISTER_GAME_PLUGIN(name, clazz)                                                                                                                   \
    class clazz##__initializer {                                                                                                                               \
    public:                                                                                                                                                    \
        clazz##__initializer() {                                                                                                                               \
            ::server::game_plugins::registry::register_(name, [](Game *game) { return std::make_unique<clazz>(game, name); });                                 \
        }                                                                                                                                                      \
    } clazz##__initializer_instance;

#else
#define LN_REGISTER_GAME_PLUGIN(name, clazz)                                                                                                                   \
    __attribute__((constructor(65530))) __attribute__((used)) __attribute__((retain)) static void register_game_plugin_##clazz() {                             \
        ::server::game_plugins::registry::register_(name, [](Game *game) { return std::make_unique<clazz>(game, name); });                                     \
    }

#endif
