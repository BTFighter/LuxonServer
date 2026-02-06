// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

#include "game_plugin_base.hpp"
#include "game.hpp"
#include "logger.hpp"

#include <format>

namespace server::game_plugins {
PluginBase::PluginBase(Game *game, std::string_view plugin_name) : game_(game) {
    log_ = create_logger(std::format("Game Plugin {} (at {}:{}/{}/{})", plugin_name, game->app->id, game->app->version, game->lobby.name, game->id));
}

ServerManager& PluginBase::get_server_manager() { return game_->app->server_manager; }
} // namespace server::game_plugins
