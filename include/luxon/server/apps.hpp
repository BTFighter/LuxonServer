#pragma once

#include "lobby.hpp"

#include <string>
#include <string_view>
#include <vector>
#include <memory>

namespace server {
class ServerManager;
struct Lobby;

class App {
    App(ServerManager& server_manager, std::string_view id, std::string_view version);

    // Non-public: Must not be modified with any peers connected, would result in dangling pointers from `Game::lobby` to here
    std::vector<Lobby> lobbies_;

public:
    ServerManager& server_manager;
    const std::string_view id, version;

    Lobby *get_default_lobby();
    std::vector<Lobby *> get_lobbies();
    std::vector<const Lobby *> get_lobbies() const;

    std::shared_ptr<App> get_shared() { return get(server_manager, std::string(id), std::string(version)); }

    static std::shared_ptr<App> get(ServerManager& server_manager, const std::string& id, const std::string& version);
    static std::vector<std::shared_ptr<App>> get_all(ServerManager& server_manager);
};
} // namespace server
