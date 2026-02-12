// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "lobby.hpp"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <memory>
#include <cstdint>

namespace server {
class ServerManager;
struct Lobby;

using LobbyId = std::pair<std::string_view, uint8_t>;

struct LobbyIdHash {
    size_t operator()(const LobbyId& k) const noexcept;
};

class App {
    App(ServerManager& server_manager, std::string_view id, std::string_view version);

    std::unordered_map<LobbyId, std::weak_ptr<Lobby>, LobbyIdHash> lobbies_;

public:
    ServerManager& server_manager;
    const std::string_view id, version;

    std::shared_ptr<Lobby> get_lobby(LobbyId id = {});
    const std::unordered_map<LobbyId, std::weak_ptr<Lobby>, LobbyIdHash>& get_lobbies() { return lobbies_; }

    std::shared_ptr<App> get_shared() { return get(server_manager, std::string(id), std::string(version)); }

    static std::shared_ptr<App> get(ServerManager& server_manager, const std::string& id, const std::string& version);
    static std::vector<std::shared_ptr<App>> get_all(ServerManager& server_manager);
};
} // namespace server
