#pragma once

#include "global.hpp"
#include "handler_base.hpp"

#include <string>
#include <functional>
#include <luxon/ser_types.hpp>

#ifdef LUXON_SERVER_ENABLE_HOOKPOINTS
namespace server {
class MasterServerHandler;

struct Hookpoints {
    std::function<bool(MasterServerHandler&, const std::string&, bool)> MasterServer_HandleOperationRequest_JoinGame;
    std::function<bool(MasterServerHandler&, const std::string&)> MasterServer_HandleOperationRequest_CreateGame;
};
} // namespace server

#define LUXON_SERVER_HOOKPOINT(name, ...)                                                                                                                      \
    if (server_manager_.hookpoints.name(*this, __VA_ARGS__))                                                                                                   \
    return
#else
#define LUXON_SERVER_HOOKPOINT(...)
#endif
