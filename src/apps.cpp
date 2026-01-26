#include "apps.hpp"
#include "lobby.hpp"
#include "server_manager.hpp"
#include "string_hash.hpp"

#include <unordered_map>
#include <utility>

namespace server {
App::App(ServerManager& server_manager, std::string_view id, std::string_view version)
    : server_manager(server_manager), id(id), version(version), lobbies_(1, {*this}) {}

Lobby *App::get_default_lobby() {
    if (lobbies_.size() == 1)
        return &lobbies_[0];
    for (Lobby& lobby : lobbies_)
        if (lobby.name.empty())
            return &lobby;
    return nullptr;
}

std::vector<Lobby *> App::get_lobbies() {
    std::vector<Lobby *> fres;
    for (Lobby& lobby : lobbies_)
        fres.push_back(&lobby);
    return fres;
}

std::vector<const Lobby *> App::get_lobbies() const {
    std::vector<const Lobby *> fres;
    for (const Lobby& lobby : lobbies_)
        fres.push_back(&lobby);
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
