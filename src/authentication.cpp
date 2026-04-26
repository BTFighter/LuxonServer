// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

#include "authentication.hpp"
#include "global.hpp"
#include "peer.hpp"
#include "peer_persistence.hpp"
#include "apps.hpp"
#include "data_model.hpp"
#include "server_manager.hpp"
#ifdef LUXON_SERVER_ENABLE_PLUGINS
#include "auth_plugin_registry.hpp"
#endif

#include <format>
#include <algorithm>
#include <random>
#include <luxon/ser_interface.hpp>
#include <luxon/common_codes.hpp>
#include <tracy/Tracy.hpp>

namespace server {
namespace models {
using namespace DictKeyCodes::LoadBalancing;

using TokenAuth = Model<Parameter<std::string, Token>>;

using StandardAuth =
    Model<Parameter<std::string, ApplicationId>, Parameter<uint8_t, DictKeyCodes::AuthAndLobby::ClientAuthenticationType, false, DefaultConst<0>>,
          Parameter<std::string, DictKeyCodes::AuthAndLobby::ClientAuthenticationParameters, false, DefaultString<"">>,
          Parameter<std::string, AppVersion, true>, Parameter<std::string, UserId, true>>;
} // namespace models

namespace {
std::string generate_user_id() {
    static std::mt19937 gen{std::random_device{}()};
    const std::string_view charset = "0123456789ABCDEF";
    std::uniform_int_distribution<size_t> dist(0, charset.size() - 1);

    std::string fres(32, '\0');
    std::ranges::generate(fres, [&] { return charset[dist(gen)]; });
    return fres;
}
} // namespace

ser::OperationResponseMessage authenticate(ServerManager& server_manager, Peer& peer, const ser::OperationRequestMessage& req,
                                           const enet::EnetCommandHeader& cmd_header, bool refresh_token) {
    ZoneScoped;

    // Stop if maximum connection count is reached
    if (server_manager.get_max_connections() != 0 && server_manager.get_connection_count() > server_manager.get_max_connections()) {
        return {.operation_code = req.operation_code,
                .return_code = ErrorCodes::Throttling::MaxCcuReached,
                .debug_message = std::format("Max CCU of {} reached", server_manager.get_max_connections())};
    }

    // Decide on algorithm based on the presence of the Token parameter
    const bool token_auth = req.parameters.contains(DictKeyCodes::LoadBalancing::Token);

    if (token_auth) {
        // Token mechanism
        const auto params = models::TokenAuth::decode(req);
        if (!params)
            return params.error();

        peer.persistent = load_persistent_peer(server_manager, params->get<DictKeyCodes::LoadBalancing::Token>(), refresh_token);
    } else {
        // Regular mechanism
        const auto params = models::StandardAuth::decode(req);
        if (!params)
            return params.error();

        auto& p = peer.persistent = create_persistent_peer();

        // Handle app version
        const std::string *version_ptr = params->get<DictKeyCodes::LoadBalancing::AppVersion>();
        const std::string app_version = version_ptr ? *version_ptr : "(poor attempt at emulating null app version)";

        p->app = App::get(server_manager, params->get<DictKeyCodes::LoadBalancing::ApplicationId>(), app_version);

        // Generate user ID if not provided
        if (const std::string *uid = params->get<DictKeyCodes::LoadBalancing::UserId>()) {
            const uint8_t auth_type = params->get<DictKeyCodes::AuthAndLobby::ClientAuthenticationType>();
            const unsigned auth_mode = server_manager.get_auth_mode(); // 0 = Weak, 1 = Anonymous, 2 = Strict

            if (auth_mode == 0 || (auth_mode == 1 && auth_type == 0)) {
                // Weak authentication (all pass) or anonymous authentication (type 0 passes)
                p->user_id = *uid;
            } else if (auth_type == 0) {
                // Type 0 specified, but server strictly requires valid authentication plugins
                return {.operation_code = req.operation_code,
                        .return_code = ErrorCodes::Auth::CustomAuthenticationFailed,
                        .debug_message = "Anonymous authentication is not allowed"};
            } else {
#ifdef LUXON_SERVER_ENABLE_PLUGINS
                const std::string_view auth_params = params->get<DictKeyCodes::AuthAndLobby::ClientAuthenticationParameters>();

                // Attempt to dispatch custom authentication via registered plugins
                if (auto plugin_res = auth_plugins::registry::call(auth_type, *uid, auth_params, "")) {
                    if (plugin_res->has_value()) {
                        p->user_id = plugin_res->value();
                    } else {
                        auto err_resp = plugin_res->error();
                        err_resp.operation_code = req.operation_code;
                        return err_resp;
                    }
                } else {
                    return {.operation_code = req.operation_code,
                            .return_code = ErrorCodes::Auth::CustomAuthenticationFailed,
                            .debug_message = "Authentication type not available"};
                }
#else
                return {.operation_code = req.operation_code,
                        .return_code = ErrorCodes::Auth::CustomAuthenticationFailed,
                        .debug_message = "Authentication plugins are disabled"};
#endif
            }
        } else {
            p->user_id = generate_user_id();
        }
    }

    // Check for success
    if (!peer.persistent) {
        // Handle authentication failure
        return {.operation_code = req.operation_code,
                .return_code = token_auth ? ErrorCodes::Auth::AuthenticationTokenExpired : ErrorCodes::Auth::InvalidAuthentication,
                .debug_message = "Authentication failure: Got no persistent peer data"};
    }

    peer.log->info("Client has authenticated as: {}", peer.persistent->user_id);

    ser::OperationResponseMessage resp{.operation_code = req.operation_code};
    if (refresh_token)
        resp.parameters[DictKeyCodes::LoadBalancing::Token] = peer.persistent->token;
    return resp;
}
} // namespace server
