// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "global.hpp"
#include "apps.hpp"
#include "lobby.hpp"
#include "peer.hpp"
#ifdef LUXON_SERVER_ENABLE_PLUGINS
#include "game_plugin_base.hpp"
#endif

#include <memory>
#include <unordered_set>
#include <vector>
#include <list>
#include <variant>
#include <luxon/ser_types.hpp>
#include <luxon/enet_peer.hpp>

#ifdef LUXON_SERVER_ENABLE_PLUGINS
#define GAME_PLUGINS_INVOKE(...)                                                                                                                               \
    {                                                                                                                                                          \
        using namespace game_plugins;                                                                                                                          \
        __VA_ARGS__                                                                                                                                            \
    }
#else
#define GAME_PLUGINS_INVOKE(...)
#endif

namespace server {
class App;
struct Lobby;
struct Peer;

struct GamePeer {
    std::weak_ptr<Peer> peer;
    int32_t actor_id{};
    ser::Hashtable actor_props;
    std::unordered_set<uint8_t> interest_groups;

    bool is_valid() const { return actor_id > 0; }
    bool has_interest_group(uint8_t group) const;
    bool disconnect();
};

struct Event {
    uint8_t code;
    int32_t sender_actor_id;
    enet::EnetDeliveryMode delivery_mode = enet::EnetDeliveryMode::Reliable;
    uint8_t channel{};
    std::variant<std::monostate, uint8_t, std::unordered_set<int32_t>> receivers{};
    uint8_t interest_group{};
    ser::Value data;
    ser::Dictionary top_params;

    ser::Hashtable& make_params_hashtable() { return *(data = std::make_shared<ser::Hashtable>()).get<ser::HashtablePtr>(); }
    ser::Hashtable *get_params_hashtable() {
        if (data.is<ser::HashtablePtr>())
            return data.get<ser::HashtablePtr>().get();
        return nullptr;
    }
};

struct Game : std::enable_shared_from_this<Game> {
    const std::shared_ptr<App> app;
    Lobby& lobby;
    const std::string id;

    ~Game();

#ifdef LUXON_SERVER_ENABLE_PLUGINS
    std::vector<std::unique_ptr<game_plugins::PluginBase>> plugins;
#endif

    uint8_t flags = 3; // CheckUserOnJoin | DeleteCacheOnLeave
    bool is_open = true;
    bool is_visible = true;
    int32_t player_ttl = 0;
    int32_t empty_game_ttl = 0;
    uint8_t max_peers = 0;
    int32_t master_actor = 1;
    int32_t last_actor_id = 0;
    std::unordered_set<std::string> expected_users;
    ser::Hashtable custom_props;
    std::vector<std::string> lobby_props;
    std::list<Event> event_cache;

    Game(std::shared_ptr<App> app, Lobby& lobby, std::string id) : app(std::move(app)), lobby(lobby), id(std::move(id)) {}

    std::list<GamePeer> peers;

    GamePeer create_peer(std::shared_ptr<Peer> peer);
    GamePeer *add_peer(GamePeer&& game_peer);
    bool remove_peer(const std::shared_ptr<Peer>& peer);
    bool flood_peer(GamePeer *game_peer);
    GamePeer *find_peer(int32_t actor_id);
    void broadcast_event(Event& event);
    int16_t validate_join(const std::string& user_id, size_t new_expected_users_count = 0) const;

    void trigger_lobby_update();

    ser::Value get_game_prop(const ser::Value& key);
    ser::Hashtable get_basic_game_props();
    ser::Hashtable get_game_props(bool no_custom = false);
    ser::Hashtable get_actor_props();
    void insert_game_props(ser::Hashtable update);
    bool expect_game_props(ser::Hashtable expected);
    void insert_actor_props(int32_t actor_id, const ser::Hashtable& update);
    bool expect_actor_props(int32_t actor_id, const ser::Hashtable& expected);

#ifdef LUXON_SERVER_ENABLE_PLUGINS
    template <typename InfoStruct>
    game_plugins::Result execute_plugin_chain(game_plugins::Result (game_plugins::PluginBase::*method)(luxon::ser::OperationRequestMessage&, InfoStruct&),
                                              luxon::ser::OperationRequestMessage& req, InfoStruct& info) {
        for (const auto& plugin : plugins) {
            game_plugins::Result result = ((*plugin).*method)(req, info);
            if (result != game_plugins::Result::Continue)
                return result;
        }

        return game_plugins::Result::Continue;
    }

    template <typename InfoStruct>
    game_plugins::Result execute_plugin_chain(game_plugins::Result (game_plugins::PluginBase::*method)(InfoStruct&), InfoStruct& info) {
        for (const auto& plugin : plugins) {
            game_plugins::Result result = ((*plugin).*method)(info);
            if (result != game_plugins::Result::Continue)
                return result;
        }

        return game_plugins::Result::Continue;
    }
#endif

    // Helper to check if event data matches a filter hashtable
    static bool matches_filter(const ser::Value& event_data, const ser::Hashtable& filter);
};
} // namespace server
