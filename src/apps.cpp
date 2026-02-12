// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

#include "apps.hpp"
#include "lobby.hpp"
#include "server_manager.hpp"
#include "string_hash.hpp"

#include <unordered_map>
#include <utility>

namespace server {
size_t LobbyIdHash::operator()(const LobbyId& k) const noexcept {
    std::size_t h1 = std::hash<std::string_view>{}(k.first);
    std::size_t h2 = std::hash<unsigned int>{}(k.second); // avoid uint8_t quirks
    // hash combine
    return h1 ^ (h2 + 0x9e3779b97f4a7c15ull + (h1 << 6) + (h1 >> 2));
}

App::App(ServerManager& server_manager, std::string_view id, std::string_view version) : server_manager(server_manager), id(id), version(version) {}

std::shared_ptr<Lobby> App::get_lobby(LobbyId id) {
    // Try to find lobby first
    auto res = lobbies_.find(id);
    if (res != lobbies_.end())
        if (auto lobby = res->second.lock())
            return lobby;

    // Create lobby
    std::shared_ptr<Lobby> fres(new Lobby(get_shared(), std::string(id.first), id.second), [this](Lobby *ptr) {
        lobbies_.erase(LobbyId{ptr->name, ptr->type});
        delete ptr;
    });
    lobbies_[{fres->name, fres->type}] = fres;
    return fres;
}

std::shared_ptr<App> App::get(ServerManager& server_manager, const std::string& id, const std::string& version) {
    {
        auto res = server_manager.apps.find({id, version});
        if (res != server_manager.apps.end()) {
            if (auto fres = res->second.lock())
                return fres;
            else
                server_manager.apps.erase(res);
        }
    }

    auto res = server_manager.apps.emplace(std::pair<std::string, std::string>(id, version), std::weak_ptr<App>());
    const auto& [allocated_id, allocated_version] = res.first->first;
    std::shared_ptr<App> fres(new App(server_manager, allocated_id, allocated_version), [&server_manager](App *ptr) {
        auto it = server_manager.apps.find({std::string(ptr->id), std::string(ptr->version)});
        if (it != server_manager.apps.end())
            server_manager.apps.erase(it);
        delete ptr;
    });
    res.first->second = fres;
    return fres;
}

std::vector<std::shared_ptr<App>> App::get_all(ServerManager& server_manager) {
    std::vector<std::shared_ptr<App>> fres;
    for (auto& [_, weak_app] : server_manager.apps)
        if (auto app = weak_app.lock())
            fres.emplace_back(std::move(app));
    return fres;
}
} // namespace server
