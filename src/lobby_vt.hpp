// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

typedef struct sqlite3 sqlite3;

namespace server {
struct Lobby;

/// Registers the 'LobbyGames' virtual table module on the given SQLite database.
void register_lobby_virtual_table(sqlite3 *db, Lobby *lobby);
} // namespace server
