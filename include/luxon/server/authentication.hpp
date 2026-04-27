// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "global.hpp"

#include <luxon/enet_protocol.hpp>
#include <luxon/ser_types.hpp>

namespace server {
class ServerManager;
class Peer;
struct AppSettings;

struct AuthProviderSettings {
    std::string appid;
    uint8_t auth_type;
    bool is_allowed;
    std::optional<std::string> secret, auth_url;
};

ser::OperationResponseMessage authenticate(ServerManager& server_manager, Peer& peer, const ser::OperationRequestMessage& req,
                                           const enet::EnetCommandHeader& cmd_header, bool refresh_token = true);
} // namespace server
