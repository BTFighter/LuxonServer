// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "global.hpp"
#include "logger.hpp"
#include "peer_persistence.hpp"

#include <memory>
#include <luxon/enet_peer.hpp>
#include <luxon/ser_interface.hpp>
#include <luxon/ser_encryption.hpp>

namespace server {
struct PeerPersistent;

struct Peer {
    std::unique_ptr<ser::IProtocol> protocol;
    std::shared_ptr<enet::EnetPeer> enet_peer;
    std::shared_ptr<logger> log;
    std::unique_ptr<PeerPersistent> persistent;

    bool is_authenticated() const { return persistent != nullptr; }
    void send(const ser::ByteArray& payload, const enet::EnetSendOptions& opt);
    void disconnect();
};
} // namespace server
