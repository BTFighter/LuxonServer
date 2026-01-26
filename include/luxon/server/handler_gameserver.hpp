#pragma once

#include "global.hpp"
#include "handler_base.hpp"

#include <luxon/ser_types.hpp>

namespace server {
class GameServerHandler : public HandlerBase {
public:
    using HandlerBase::HandlerBase;

    void HandleDisconnect() override;
    void HandleOperationRequest(ser::OperationRequestMessage& req, const enet::EnetCommandHeader& cmd_header) override;

    auto& get_game() { return peer_->persistent->current_game; }

protected:
    GamePeer *game_peer_{};
    bool has_left{};
};
} // namespace server
