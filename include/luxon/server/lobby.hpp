// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <string>
#include <memory>
#include <list>
#include <functional>
#include <unordered_map>
#include <cstdint>

typedef struct sqlite3 sqlite3;

namespace server {
class App;
struct Lobby;
struct Game;

struct GameListUpdateHandler {
    std::function<void(const std::shared_ptr<Game>&)> game_create;
    std::function<void(const std::shared_ptr<Game>&)> game_change;
    std::function<void(Game *)> game_delete;
};

struct Lobby : std::enable_shared_from_this<Lobby> {
    Lobby(std::shared_ptr<App> app, std::string name, uint8_t type = 0);

    const std::shared_ptr<App> app;
    const std::string name;
    const uint8_t type;

    std::unordered_map<std::string_view, std::weak_ptr<Game>> games;
    std::list<GameListUpdateHandler> game_list_update_handlers;

    sqlite3 *sql{};

    std::shared_ptr<Game> create_game(std::string id, bool or_get = false);

    size_t get_peer_count() const;
    size_t get_master_peer_count() const;

    void sql_create_game(const std::string& id);
    void sql_update_game_property(const std::string& id, char c_digit, const std::string& value);
    void sql_delete_game(const std::string& id);
};
} // namespace server
