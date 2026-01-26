#include "authentication.hpp"
#include "global.hpp"
#include "codes.hpp"
#include "peer.hpp"
#include "peer_persistence.hpp"
#include "apps.hpp"

#include <algorithm>
#include <random>
#include <luxon/ser_interface.hpp>

namespace server {
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
    // Decide on algorithm
    const bool token_auth = req.parameters.contains(DictKeyCodes::LoadBalancing::Token);

    // Try authentication
    try {
        if (token_auth) {
            // Token mechanism
            peer.persistent = load_persistent_peer(server_manager, req.parameters.at(DictKeyCodes::LoadBalancing::Token).get<std::string>(), refresh_token);
        } else {
            // Regular mechanism (very simple for now)
            auto& p = peer.persistent = create_persistent_peer();
            p->app = App::get(server_manager, req.parameters.at(DictKeyCodes::LoadBalancing::ApplicationId).get<std::string>(),
                              req.parameters.at(DictKeyCodes::LoadBalancing::AppVersion).get<std::string>());
            if (req.parameters.contains(DictKeyCodes::LoadBalancing::UserId))
                p->user_id = req.parameters.at(DictKeyCodes::LoadBalancing::UserId).get<std::string>();
            else
                p->user_id = generate_user_id();
        }
    } catch (const std::out_of_range& e) {
        // Handle bad parameter map access
        return {.operation_code = req.operation_code,
                .return_code = ErrorCodes::Data::InvalidRequestParameters,
                .debug_message = std::format("Missing parameter: {}", e.what())};
    } catch (const std::bad_variant_access& e) {
        // Handle bad parameter variant access
        return {.operation_code = req.operation_code,
                .return_code = ErrorCodes::Data::InvalidRequestParameters,
                .debug_message = std::format("Invalid parameter type: {}", e.what())};
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
