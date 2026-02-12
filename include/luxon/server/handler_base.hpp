// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "global.hpp"
#include "peer.hpp"

#include <memory>
#include <luxon/ser_types.hpp>
#include <luxon/enet_protocol.hpp>
#include <luxon/enet_peer.hpp>
#include <luxon/ser_encryption.hpp>
#include <luxon/http_parser.hpp>

namespace server {
class ServerManager;

class HandlerBase {
public:
    HandlerBase(ServerManager& server_manager, std::shared_ptr<Peer> peer) : server_manager_(server_manager), peer_(std::move(peer)), proto_(peer_->protocol) {}
    virtual ~HandlerBase();

    virtual void HandleConnect();
    virtual void HandleDisconnect();
    virtual void HandleUpdate();
    virtual void HandleSlowUpdate();
    virtual void HandleENetConnectionStateChange(enet::EnetConnectionState state);
    virtual void HandleENetCommand(const enet::EnetCommand& cmd);
    virtual void HandleHTTPRequest(const HttpRequest& request, const enet::EnetCommandHeader& cmd_header);
    virtual void HandleInitRequest(const ser::InitMessage& req, const enet::EnetCommandHeader& cmd_header);
    virtual void HandleOperationRequest(const ser::OperationRequestMessage& req, bool is_encrypted, const enet::EnetCommandHeader& cmd_header);
    virtual void HandleInternalOperationRequest(const ser::InternalOperationRequestMessage& req, bool is_encrypted, const enet::EnetCommandHeader& cmd_header);

    const std::shared_ptr<Peer>& get_peer() const { return peer_; }

    void send(const ser::ByteArray& payload, const enet::EnetSendOptions& opt = {0});
    void send(const std::expected<ser::ByteArray, ser::Error>& expected_payload, const enet::EnetSendOptions& opt = {0});

protected:
    ServerManager& server_manager_;
    std::shared_ptr<Peer> peer_;
    std::unique_ptr<ser::IProtocol>& proto_;
};
} // namespace server
