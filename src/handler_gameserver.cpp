// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

#include "handler_gameserver.hpp"
#include "game.hpp"
#ifdef LUXON_SERVER_ENABLE_PLUGINS
#include "game_plugin_registry.hpp"
#include "game_plugin_base.hpp"
#endif
#include "global.hpp"
#include "codes.hpp"
#include "authentication.hpp"

#include <ranges>
#include <luxon/ser_interface.hpp>

namespace server {
void GameServerHandler::HandleDisconnect() {
    if (auto& game = get_game()) {
        // Cleanup cache if enabled
        if (game->flags & DictKeyCodes::RoutingAndEvents::CleanupCacheOnLeave)
            game->event_cache.remove_if([&](const Event& cached_event) { return cached_event.sender_actor_id == game_peer_->actor_id; });

        if (game_peer_) {
            if (!has_left) {
                // Call into plugins
                GAME_PLUGINS_INVOKE({
                    OnLeaveGameCallInfo info{.leaver = game_peer_};
                    ser::OperationRequestMessage req{.operation_code = 0};
                    game->execute_plugin_chain(&PluginBase::OnLeave, req, info);
                })
            }

            // Remove peer
            const int32_t actor_id = game_peer_ ? game_peer_->actor_id : 0;
            const bool was_master = actor_id == game->master_actor;
            if (!game->remove_peer(peer_))
                peer_->log->warn("Failed to remove peer from game");

            // Broadcast leave event
            if (!(game->flags & GameFlags::SuppressRoomEvents)) {
                std::vector<int32_t> actor_ids;
                for (auto& game_peer : game->peers)
                    actor_ids.push_back(game_peer.actor_id);

                Event event{.code = EventCodes::Leave, .sender_actor_id = actor_id, .receivers = ReceiverGroup::All};
                event.top_params[DictKeyCodes::GameAndActor::ActorNo] = actor_id;
                event.top_params[DictKeyCodes::GameAndActor::ActorList] = actor_ids;
                if (was_master)
                    event.top_params[GameProps::MasterClientId] = game->master_actor;
                get_game()->broadcast_event(event);
            }
        }

        peer_->persistent->current_game.reset();
        game.reset();
    }
}

void GameServerHandler::HandleOperationRequest(ser::OperationRequestMessage& req, bool is_encrypted, const enet::EnetCommandHeader& cmd_header) {
    const auto ensure_is_master = [&]() {
        const bool is_master = game_peer_ && game_peer_->actor_id == get_game()->master_actor || get_game()->peers.size() == 0;
        if (!is_master) {
            const ser::OperationResponseMessage resp{
                .operation_code = req.operation_code, .return_code = ErrorCodes::Core::OperationNotAllowedInCurrentState, .debug_message = "Must be master"};
            send(proto_->Serialize(resp), enet::EnetSendOptions{cmd_header.channel_id});
            return false;
        }
        return true;
    };
    const auto ensure_joined_state = [&](bool joined = true) {
        if ((game_peer_ && game_peer_->actor_id != 0) != joined) {
            const ser::OperationResponseMessage resp{
                .operation_code = req.operation_code, .return_code = ErrorCodes::Core::OperationNotAllowedInCurrentState, .debug_message = "Must join first"};
            send(proto_->Serialize(resp), enet::EnetSendOptions{cmd_header.channel_id});
            return false;
        }
        return true;
    };

    if (!peer_->is_authenticated()) {
        if (cmd_header.channel_id != 0)
            return HandlerBase::HandleOperationRequest(req, is_encrypted, cmd_header);

        switch (req.operation_code) {

        case OpCodes::Auth::Authenticate:
        case OpCodes::Auth::AuthenticateOnce: {
            // Try to authenticate
            auto resp = authenticate(server_manager_, *peer_, req, cmd_header, false);

            // Add position parameter if authentication was successful
            if (resp.return_code == ErrorCodes::Core::Ok)
                resp.parameters[DictKeyCodes::LoadBalancing::Position] = static_cast<int32_t>(0);

            // Send response
            send(proto_->Serialize(resp, is_encrypted));
            return;
        }
        }
    } else if (auto game = get_game()) {

        if (req.operation_code == OpCodes::Lite::RaiseEvent) {
            if (!ensure_joined_state())
                return;

            const auto& actors_param = req.parameters[DictKeyCodes::GameAndActor::ActorList];
            const auto receiver_group = req.parameters[DictKeyCodes::RoutingAndEvents::ReceiverGroup].get_or<uint8_t>(0);
            const auto interest_group = req.parameters[DictKeyCodes::RoutingAndEvents::InterestGroup].get_or<uint8_t>(0);
            const auto cache_op = req.parameters[DictKeyCodes::RoutingAndEvents::Cache].get_or<uint8_t>(CacheOperation::DoNotCache);
            auto data_param = req.parameters[DictKeyCodes::RoutingAndEvents::Data];
            const auto code = req.parameters[DictKeyCodes::RoutingAndEvents::Code].get_or<uint8_t>(200);

            // Build event
            Event event;
            event.sender_actor_id = game_peer_->actor_id;
            event.code = code;
            event.delivery_mode = enet::FlagsToEnetDeliveryMode(cmd_header.flags);
            event.interest_group = interest_group;
            event.channel = cmd_header.channel_id;
            if (auto *actors = actors_param.get_ptr<std::vector<int32_t>>())
                event.receivers = *actors | std::ranges::to<std::unordered_set>();
            else
                event.receivers = receiver_group;
            event.data = std::move(data_param);

            // Call into plugins
            GAME_PLUGINS_INVOKE({
                OnRaiseEventCallInfo info{.raiser = game_peer_, .event = event, .cache_op = cache_op};
                const Result res = game->execute_plugin_chain(&PluginBase::OnRaiseEvent, req, info);

                if (res == Result::Cancel)
                    return;
                if (res == Result::Fail) {
                    const ser::OperationResponseMessage resp{.operation_code = OpCodes::Lite::RaiseEvent,
                                                         .return_code = ErrorCodes::Matchmaking::PluginReportedError};
                    send(proto_->Serialize(resp), enet::EnetSendOptions{.channel = cmd_header.channel_id});
                    return;
                }
            });

            // Make sure client isn't attempting to raise a Photon event
            if (event.code > 220) {
                const ser::OperationResponseMessage resp{.operation_code = OpCodes::Lite::RaiseEvent,
                                                         .return_code = ErrorCodes::Core::OperationInvalid,
                                                         .debug_message = "Not allowed to raise Photon events (codes higher than 220)"};
                send(proto_->Serialize(resp), enet::EnetSendOptions{.channel = cmd_header.channel_id});
                return;
            }

            // RemoveFromRoomCache
            if (cache_op == CacheOperation::RemoveFromRoomCache) {
                std::vector<int32_t> filter_senders;
                // Use target actors option to specify the sender number
                if (auto *actors = actors_param.get_ptr<std::vector<int32_t>>())
                    filter_senders = *actors;

                ser::Hashtable filter_data;
                if (data_param.is<ser::HashtablePtr>())
                    if (auto ptr = data_param.get<ser::HashtablePtr>())
                        filter_data = *ptr;

                // Event code 0 is wildcard
                const bool wildcard_code = code == 0;

                game->event_cache.remove_if([&](const Event& cached_event) {
                    // Code Filter
                    if (!wildcard_code && cached_event.code != code)
                        return false;

                    // Sender Filter
                    if (!filter_senders.empty()) {
                        bool found = false;
                        for (int32_t id : filter_senders) {
                            if (id == cached_event.sender_actor_id) {
                                found = true;
                                break;
                            }
                        }
                        if (!found)
                            return false;
                    }

                    // Data filter (subset match)
                    if (!filter_data.empty())
                        if (!Game::matches_filter(cached_event.data, filter_data))
                            return false;

                    return true;
                });
                return; // Do NOT broadcast removal
            }

            // RemoveFromCacheForActorsLeft
            if (cache_op == CacheOperation::RemoveFromCacheForActorsLeft) {
                game->event_cache.remove_if([&](const Event& cached_event) {
                    return game->find_peer(cached_event.sender_actor_id) == nullptr && cached_event.sender_actor_id != 0; // Don't remove global
                });
                return;
            }

            // Add To Cache
            bool can_cache = (cache_op == CacheOperation::AddToRoomCache || cache_op == CacheOperation::AddToRoomCacheGlobal);
            if (can_cache && actors_param.get_ptr<std::vector<int32_t>>() == nullptr && receiver_group != ReceiverGroup::MasterClient && interest_group == 0) {
                Event cached_copy = event; // Making copy to allow change below to happen non-destructively
                if (cache_op == CacheOperation::AddToRoomCacheGlobal)
                    cached_copy.sender_actor_id = 0; // Can not be traced back

                game->event_cache.emplace_back(std::move(cached_copy));
            }

            // Broadcast
            game->broadcast_event(event);
            return;
        }

        if (cmd_header.channel_id != 0)
            return HandlerBase::HandleOperationRequest(req, is_encrypted, cmd_header);

        switch (req.operation_code) {

        case OpCodes::Matchmaking::CreateGame:
        case OpCodes::Matchmaking::JoinGame: {
            // Common validation
            if (!ensure_joined_state(false))
                return;

            const bool is_master = game->peers.empty();
            const auto broadcast = req.parameters[DictKeyCodes::RoutingAndEvents::Broadcast].get_or<bool>(true);
            const std::string& game_id = req.parameters[DictKeyCodes::GameAndActor::GameId].get_or<std::string>(game->id);

            // Validate game ID
            if (game_id != game->id) {
                const ser::OperationResponseMessage resp{.operation_code = req.operation_code,
                                                         .return_code = ErrorCodes::Matchmaking::GameIdNotExists,
                                                         .debug_message = "Token not valid for this Game ID"};
                send(proto_->Serialize(resp));
                return;
            }

            if (is_master) {
                // Load given plugins if creating room
                const auto& plugins = req.parameters[DictKeyCodes::RpcAndPlugins::Plugins].get_or<std::vector<std::string>>();
                for (const std::string& plugin_name : plugins) {
#ifdef LUXON_SERVER_ENABLE_PLUGINS
                    auto plugin = game_plugins::registry::instantiate(get_game().get(), plugin_name);
                    if (!plugin) {
                        peer_->log->warn("Attempting to load unknown game plugin: {}", plugin_name);
                        continue;
                    }

                    get_game()->plugins.emplace_back(std::move(plugin))->OnAttach();
#else
                peer_->log->warn("Attempting to load game plugin when plugins are disabled: {}", plugin_name);
#endif
                }
            } else {
                // Verify join if joining existing room
                const int16_t join_validation_code = game->validate_join(peer_->persistent->user_id);
                if (join_validation_code != ErrorCodes::Core::Ok) {
                    const ser::OperationResponseMessage resp{
                        .operation_code = OpCodes::Matchmaking::JoinGame, .return_code = join_validation_code, .debug_message = "Game closed or full"};
                    send(proto_->Serialize(resp));
                    return;
                }
            }

            // Call into plugins
            GAME_PLUGINS_INVOKE({
                auto& game = get_game();
                Result res;

                if (game->peers.empty()) {
                    OnCreateGameCallInfo info{.creator = peer_,
                                              .is_join = req.operation_code == OpCodes::Matchmaking::JoinGame,
                                              .create_if_not_exist = req.parameters[DictKeyCodes::AuthAndLobby::CreateIfNotExists].get_or<bool>(false)};
                    res = game->execute_plugin_chain(&PluginBase::OnCreateGame, req, info);
                } else {
                    BeforeJoinGameCallInfo info{.joiner = peer_};
                    res = game->execute_plugin_chain(&PluginBase::BeforeJoin, req, info);
                }

                if (res == Result::Fail) {
                    const ser::OperationResponseMessage resp{.operation_code = req.operation_code, .return_code = ErrorCodes::Matchmaking::PluginReportedError};
                    send(proto_->Serialize(resp));
                    return;
                }
            })

            // Apply game settings
            if (is_master) { // TODO: Make sure only master can set these options in reference impl!
                req.parameters[DictKeyCodes::GameSettings::PlayerTTL].store_if<int32_t>(game->player_ttl);
                req.parameters[DictKeyCodes::GameSettings::EmptyRoomTTL].store_if<int32_t>(game->empty_game_ttl);

                if (auto *flags = req.parameters[DictKeyCodes::GameSettings::GameFlags].get_ptr<int32_t>())
                    game->flags = *flags;

                auto set_flag = [&](int32_t flag, bool *value) {
                    if (!value)
                        return;
                    if (*value)
                        game->flags |= flag;
                    else
                        game->flags &= ~flag;
                };

                set_flag(GameFlags::CheckUserOnJoin, req.parameters[DictKeyCodes::GameSettings::CheckUserOnJoin].get_ptr<bool>());
                set_flag(GameFlags::SuppressRoomEvents, req.parameters[DictKeyCodes::RoutingAndEvents::SuppressRoomEvents].get_ptr<bool>());
                set_flag(GameFlags::PublishUserId, req.parameters[DictKeyCodes::RoutingAndEvents::PublishUserId].get_ptr<bool>());
            }

            // We capture props here so the response only contains the list of OTHER players if joining
            auto all_actor_props = game->get_actor_props();

            // Create peer for game
            auto game_peer = get_game()->create_peer(peer_);
            if (!game_peer.is_valid()) {
                peer_->log->error("Game peer could not be created. Connection must terminate now.");
                peer_->disconnect();
                return;
            }

            // Call into plugins
            bool broadcast_actor_props = true;
            GAME_PLUGINS_INVOKE({
                OnJoinGameCallInfo info{.joiner = &game_peer};
                const Result res = game->execute_plugin_chain(&PluginBase::OnJoinGame, req, info);

                if (res == Result::Fail) {
                    peer_->log->info("Reverting join", game->id);
                    peer_->disconnect();

                    const ser::OperationResponseMessage resp{.operation_code = req.operation_code, .return_code = ErrorCodes::Matchmaking::PluginReportedError};
                    send(proto_->Serialize(resp));
                    return;
                }

                if (!info.publish_user_id.value_or(true))
                    game_peer.actor_props.erase(ActorProps::UserId);

                broadcast_actor_props = info.broadcast_actor_props.value_or(true);
            })

            // Add peer to game
            game_peer_ = game->add_peer(std::move(game_peer));
            if (!game_peer_) {
                peer_->log->error("Player could not be added to game. Connection must terminate now.");
                peer_->disconnect();
                return;
            }
            peer_->log->info("Successfully joined game: {}", game->id);

            // Update properties
            const auto& game_props = req.parameters[DictKeyCodes::Properties::GameProperties].get_or<ser::HashtablePtr>(nullptr);
            const auto& actor_props = req.parameters[DictKeyCodes::Properties::ActorProperties].get_or<ser::HashtablePtr>(nullptr);

            if (actor_props)
                game->insert_actor_props(game_peer_->actor_id, *actor_props);
            if (game_props && is_master)
                game->insert_game_props(*game_props);

            // Construct response
            ser::OperationResponseMessage resp;
            resp.operation_code = req.operation_code;
            resp.return_code = ErrorCodes::Core::Ok;

            std::vector<int32_t> actor_ids;
            if (!(game->flags & GameFlags::SuppressRoomEvents))
                for (auto& game_peer : game->peers)
                    actor_ids.push_back(game_peer.actor_id);

            resp.parameters[DictKeyCodes::GameSettings::GameFlags] = static_cast<int32_t>(game->flags);
            if (!(game->flags & GameFlags::SuppressRoomEvents))
                resp.parameters[DictKeyCodes::GameAndActor::ActorList] = actor_ids;
            resp.parameters[DictKeyCodes::Properties::GameProperties] = game->get_game_props();
            resp.parameters[DictKeyCodes::GameAndActor::ActorNo] = game_peer_->actor_id;
            if (broadcast_actor_props)
                resp.parameters[DictKeyCodes::Properties::ActorProperties] = std::move(all_actor_props);

            send(proto_->Serialize(resp));

            // Broadcast Join Event
            if (!(game->flags & GameFlags::SuppressRoomEvents)) {
                Event event{.code = EventCodes::Join, .sender_actor_id = game_peer_->actor_id, .receivers = ReceiverGroup::All};
                event.top_params[DictKeyCodes::GameAndActor::ActorList] = actor_ids;
                event.top_params[DictKeyCodes::GameAndActor::ActorNo] = game_peer_->actor_id;
                if (broadcast_actor_props && !(game->flags & GameFlags::SuppressPlayerInfo))
                    event.top_params[DictKeyCodes::Properties::ActorProperties] = game_peer_->actor_props;

                game->broadcast_event(event);
            }

            // Flood the client with current state
            game->flood_peer(game_peer_);

            return;
        }

        case OpCodes::Lite::Leave: {
            // Call into plugins
            GAME_PLUGINS_INVOKE({
                OnLeaveGameCallInfo info{.leaver = game_peer_};
                const Result res = game->execute_plugin_chain(&PluginBase::OnLeave, req, info);

                if (res == Result::Fail) {
                    const ser::OperationResponseMessage resp{.operation_code = OpCodes::Lite::Leave, .return_code = ErrorCodes::Matchmaking::PluginReportedError};
                    send(proto_->Serialize(resp));
                    return;
                }
            })

            // Send success response
            const ser::OperationResponseMessage resp{.operation_code = OpCodes::Lite::Leave, .return_code = ErrorCodes::Core::Ok};
            send(proto_->Serialize(resp));
            return;

            // Disconnect, handler will do the rest
            has_left = true;
            peer_->disconnect();

            return;
        }

        case OpCodes::Lite::SetProperties: {
            if (!ensure_joined_state())
                return;

            auto broadcast = req.parameters[DictKeyCodes::RoutingAndEvents::Broadcast].get_or<bool>(true);
            auto actor_id = req.parameters[DictKeyCodes::GameAndActor::ActorNo].get_or<int32_t>(0);

            // Can only set non-self props as master
            if (actor_id != game_peer_->actor_id && !ensure_is_master())
                return;

            const auto props = req.parameters[DictKeyCodes::Properties::Properties].get_or<ser::HashtablePtr>(nullptr);
            const auto props_expected = req.parameters[DictKeyCodes::Properties::ExpectedValues].get_or<ser::HashtablePtr>(nullptr);

            // Call into plugins
            GAME_PLUGINS_INVOKE({
                BeforeSetPropertiesCallInfo info{
                    .setter = game_peer_, .broadcast = broadcast, .target_actor_id = actor_id, .update = props, .expected = props_expected};
                const Result res = game->execute_plugin_chain(&PluginBase::BeforeSetProperties, req, info);

                if (res == Result::Fail) {
                    const ser::OperationResponseMessage resp{.operation_code = OpCodes::Lite::SetProperties,
                                                         .return_code = ErrorCodes::Matchmaking::PluginReportedError};
                    send(proto_->Serialize(resp));
                    return;
                }

                broadcast = info.broadcast;
                actor_id = info.target_actor_id;
            });

            // Set actor or game properties
            bool ok = true;
            if (actor_id) {
                if (props_expected)
                    ok = game->expect_actor_props(actor_id, *props_expected);
                if (ok)
                    game->insert_actor_props(actor_id, *props);
            } else {
                if (props_expected)
                    ok = game->expect_game_props(*props_expected);
                if (ok)
                    game->insert_game_props(*props);
            }

            // Emtpy response
            ser::OperationResponseMessage resp;
            resp.operation_code = OpCodes::Lite::SetProperties;
            resp.return_code = ok ? ErrorCodes::Core::Ok : ErrorCodes::Core::OperationInvalid;
            send(proto_->Serialize(resp));

            // Broadcast property updates
            if (ok && broadcast) {
                Event event{.code = EventCodes::PropertiesUpdate,
                            .sender_actor_id = game_peer_->actor_id,
                            .receivers = (game->flags & GameFlags::BroadcastPropsChangeToAll) ? ReceiverGroup::All : ReceiverGroup::Others};
                if (actor_id)
                    event.top_params[DictKeyCodes::GameAndActor::TargetActorNo] = actor_id;
                event.top_params[DictKeyCodes::Properties::Properties] = *props;
                game->broadcast_event(event);
            }

            // Call into plugins
            GAME_PLUGINS_INVOKE({
                OnSetPropertiesCallInfo info{
                    .setter = game_peer_, .broadcast = broadcast, .target_actor_id = actor_id, .update = props, .expected = props_expected};
                const Result res = game->execute_plugin_chain(&PluginBase::OnSetProperties, req, info);

                if (res == Result::Fail)
                    peer_->log->error("Plugin reported error for SetProperties after properties were already set");
            });

            return;
        }

        case OpCodes::Lite::ChangeInterestGroups: {
            const auto& add_param = req.parameters[DictKeyCodes::RoutingAndEvents::Add];
            const auto& remove_param = req.parameters[DictKeyCodes::RoutingAndEvents::Remove];

            if (!add_param.is<ser::ByteArray>() || !remove_param.is<ser::ByteArray>()) {
                const ser::OperationResponseMessage resp{.operation_code = OpCodes::Lite::ChangeInterestGroups,
                                                         .return_code = ErrorCodes::Data::InvalidRequestParameters,
                                                         .debug_message = "Bad parameter type, expected byte array"};
                send(proto_->Serialize(resp));
                return;
            }

            // Remove first, then add (TODO: Verify order)
            for (const uint8_t group : remove_param.get_or<std::vector<uint8_t>>())
                game_peer_->interest_groups.erase(group);
            for (const uint8_t group : add_param.get_or<std::vector<uint8_t>>())
                game_peer_->interest_groups.insert(group);

            return;
        }
        }
    }

    return HandlerBase::HandleOperationRequest(req, is_encrypted, cmd_header);
}
} // namespace server
