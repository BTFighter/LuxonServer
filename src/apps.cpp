// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

#include "apps.hpp"
#include "lobby.hpp"
#include "server_manager.hpp"
#include "string_hash.hpp"

#include <unordered_map>
#include <utility>

namespace server {
App::App(ServerManager& server_manager, std::string_view id, std::string_view version) : server_manager(server_manager), id(id), version(version) {}

std::shared_ptr<Lobby> App::get_lobby(std::string_view name) {
    // Try to find lobby first
    auto res = lobbies_.find(name);
    if (res != lobbies_.end())
        if (auto lobby = res->second.lock())
            return lobby;

    // Create lobby
    std::shared_ptr<Lobby> fres(new Lobby(get_shared(), std::string(name)), [this](Lobby *ptr) {
        lobbies_.erase(ptr->name);
        delete ptr;
    });
    lobbies_[fres->name] = fres;
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
