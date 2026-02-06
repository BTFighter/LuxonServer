// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "global.hpp"
#include "handler_base.hpp"

namespace server {
class NameServerHandler : public HandlerBase {
public:
    using HandlerBase::HandlerBase;

    void HandleOperationRequest(ser::OperationRequestMessage& req, const enet::EnetCommandHeader& cmd_header) override;
};
} // namespace server
