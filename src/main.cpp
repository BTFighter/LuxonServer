// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

#include "luxon/server/server_manager.hpp"
#include "platform.hpp"

#ifdef __wasi__
#include <print>
#include <exception>
#endif

int main() {
    Platform P;
#ifdef __wasi__
    try {
#endif
        server::ServerManager("config.yml").run();
#ifdef __wasi__
    } catch (const std::exception& e) {
        std::println("std::terminate about to be called: {}", e.what());
        throw;
    }
#endif
}
