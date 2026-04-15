// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

#include "peer_persistence.hpp"
#include "server_manager.hpp"
#include "string_hash.hpp"

#include <vector>
#include <random>
#include <algorithm>
#include <tracy/Tracy.hpp>

namespace server {
namespace {
std::string create_token(size_t length = 32) {
    static std::random_device gen;
    const std::string_view charset = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::uniform_int_distribution<size_t> dist(0, charset.size() - 1);

    std::string s(length, '\0');
    std::ranges::generate(s, [&] { return charset[dist(gen)]; });
    return s;
}
} // namespace

void store_persistent_peer(ServerManager& server_manager, std::unique_ptr<PeerPersistent>&& pp) {
    ZoneScoped;

    server_manager.add_scheduled_task(30000, [&server_manager, token = string_hash(pp->token)]() {
        // If persistent peer has not been loaded back within 30 seconds, get rid of it
        std::erase_if(server_manager.peer_persistent_data, [token](const auto& v) { return string_hash(v->token) == token; });
    });
    server_manager.peer_persistent_data.emplace_back(std::move(pp));
}

std::unique_ptr<PeerPersistent> load_persistent_peer(ServerManager& server_manager, std::string_view token, bool refresh_token) {
    ZoneScoped;

    for (auto it = server_manager.peer_persistent_data.begin(); it != server_manager.peer_persistent_data.end(); ++it) {
        if (it->get()->token != token)
            continue;

        auto fres = std::move(*it);
        server_manager.peer_persistent_data.erase(it);
        if (refresh_token)
            fres->token = create_token();
        return fres;
    }

    return nullptr;
}

std::unique_ptr<PeerPersistent> create_persistent_peer() {
    ZoneScoped;

    auto fres = std::make_unique<PeerPersistent>();
    fres->token = create_token();
    return fres;
}
} // namespace server
