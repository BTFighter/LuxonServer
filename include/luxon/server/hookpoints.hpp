#pragma once

#ifdef LUXON_SERVER_ENABLE_HOOKPOINTS
#include "global.hpp"
#include "handler_base.hpp"

#include <string>
#include <functional>
#include <luxon/ser_types.hpp>

namespace luxon::enet {
class EnetCommandHeader;
}

namespace server {
class HandlerBase;
class MasterServerHandler;

struct Hookpoints {
    std::function<bool(MasterServerHandler&, const std::string&, bool)> MasterServer_HandleOperationRequest_JoinGame;
    std::function<bool(MasterServerHandler&, const std::string&)> MasterServer_HandleOperationRequest_CreateGame;
    std::function<bool(HandlerBase&, ser::Message&, enet::EnetCommandHeader&)> HandlerBase_HandleENetCommand_OnMessage;
};
} // namespace server

#define LUXON_SERVER_HOOKPOINT(name, ...)                                                                                                                      \
    if (server_manager_.hookpoints.name && server_manager_.hookpoints.name(*this, __VA_ARGS__))                                                                \
    return
#else
#define LUXON_SERVER_HOOKPOINT(...)
#endif
