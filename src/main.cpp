// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

#include "luxon/server/server_manager.hpp"
#include "platform.hpp"

#include <optional>
#include <csignal>

int main() {
    Platform P;
    server::ServerManager("config.yml").run();
}
