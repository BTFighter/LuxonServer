// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

#include "lobby.hpp"
#include "apps.hpp"
#include "game.hpp"

namespace server {
std::shared_ptr<Game> Lobby::create_game(std::string id, bool or_get) {
    auto res = games.find(id);
    if (res != games.end())
        return or_get ? res->second.lock() : nullptr;

    std::shared_ptr<Game> fres(new Game(app.get_shared(), *this, std::move(id)), [](Game *ptr) {
        auto& lobby = ptr->lobby;

        for (auto& handler : lobby.game_list_update_handlers)
            handler.game_delete(ptr);

        auto& games = lobby.games;
        auto res = games.find(ptr->id);
        if (res != games.end())
            games.erase(res);

        delete ptr;
    });
    games.emplace(fres->id, fres);

    for (auto& handler : game_list_update_handlers)
        handler.game_create(fres);

    return fres;
}

size_t Lobby::get_peer_count() const {
    size_t fres = 0;
    for (auto& [name, weak_game] : games)
        if (auto game = weak_game.lock())
            fres += game->peers.size();
    return fres;
}

size_t Lobby::get_master_peer_count() const {
    size_t fres = 0;
    for (auto& [name, weak_game] : games)
        if (auto game = weak_game.lock())
            fres += !!game->find_peer(game->master_actor);
    return fres;
}
} // namespace server
