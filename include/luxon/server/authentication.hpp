#pragma once

#include "global.hpp"

#include <luxon/enet_protocol.hpp>
#include <luxon/ser_types.hpp>

namespace server {
class ServerManager;
class Peer;

ser::OperationResponseMessage authenticate(ServerManager& server_manager, Peer& peer, const ser::OperationRequestMessage& req,
                                           const enet::EnetCommandHeader& cmd_header, bool refresh_token = true);
} // namespace server
