// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

#include "lobby.hpp"
#include "apps.hpp"
#include "game.hpp"
#include "sqlite3.h"

#include <memory>
#include <stdexcept>
#include <luxon/common_codes.hpp>

namespace server {
struct SQLFinalize {
    sqlite3_stmt *& s;
    ~SQLFinalize() {
        if (s) {
            sqlite3_finalize(s);
            s = nullptr;
        }
    }
};

Lobby::Lobby(std::shared_ptr<App> app, std::string name, uint8_t type) : app(std::move(app)), name(std::move(name)), type(type) {
    if (type == LobbyType::SqlLobby) {
        // Open the in-memory database
        int status = sqlite3_open(":memory:", &sql);
        if (status != SQLITE_OK)
            throw std::runtime_error(std::format("Failed to create SQLite DB for SQL lobby: {}", sqlite3_errstr(status)));

        // Initialize database
        constexpr const char *init_seq = R"(
            CREATE TABLE Games (
                    __id TEXT UNIQUE,
                    C0 TEXT, C1 TEXT, C2 TEXT, C3 TEXT, C4 TEXT,
                    C5 TEXT, C6 TEXT, C7 TEXT, C8 TEXT, C9 TEXT
            );
        )";
        char *error_message;
        status = sqlite3_exec(sql, init_seq, NULL, nullptr, &error_message);
        if (status != SQLITE_OK) {
            const std::unique_ptr<char, decltype(&sqlite3_free)> unique_error_message(error_message, sqlite3_free);
            throw std::runtime_error(std::format("Failed to initialize SQLite DB for SQL lobby: {}", error_message));
        }
    }
}

std::shared_ptr<Game> Lobby::create_game(std::string id, bool or_get) {
    auto res = games.find(id);
    if (res != games.end())
        return or_get ? res->second.lock() : nullptr;

    std::shared_ptr<Game> fres(new Game(shared_from_this(), std::move(id)), [](Game *ptr) {
        auto& lobby = ptr->lobby;

        for (auto& handler : lobby->game_list_update_handlers)
            handler.game_delete(ptr);

        auto& games = lobby->games;
        auto res = games.find(ptr->id);
        if (res != games.end())
            games.erase(res);

        delete ptr;
    });
    games.emplace(fres->id, fres);

    for (auto& handler : game_list_update_handlers)
        handler.game_create(fres);

    return fres;
}

size_t Lobby::get_peer_count() const {
    size_t fres = 0;
    for (auto& [name, weak_game] : games)
        if (auto game = weak_game.lock())
            fres += game->peers.size();
    return fres;
}

size_t Lobby::get_master_peer_count() const {
    size_t fres = 0;
    for (auto& [name, weak_game] : games)
        if (auto game = weak_game.lock())
            fres += !!game->find_peer(game->master_actor);
    return fres;
}

void Lobby::sql_create_game(const std::string& id) {
    if (!sql)
        throw std::runtime_error("Not an SQL capable lobby!");

    sqlite3_stmt *statement = nullptr;
    SQLFinalize finalize{statement};

    const char *sql_text = "INSERT INTO Games(__id) VALUES(?1);";
    int status = sqlite3_prepare_v2(sql, sql_text, -1, &statement, nullptr);
    if (status != SQLITE_OK)
        throw std::runtime_error(std::format("Failed to prepare INSERT for SQL lobby: {}", sqlite3_errmsg(sql)));

    status = sqlite3_bind_text(statement, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    if (status != SQLITE_OK)
        throw std::runtime_error(std::format("Failed to bind id for SQL lobby: {}", sqlite3_errmsg(sql)));

    status = sqlite3_step(statement);
    if (status != SQLITE_DONE)
        throw std::runtime_error(std::format("Failed to insert game into SQL lobby: {}", sqlite3_errmsg(sql)));
}

void Lobby::sql_update_game_property(const std::string& id, char c_digit, const std::string& value) {
    if (!sql)
        throw std::runtime_error("Not an SQL capable lobby!");

    if (c_digit < '0' || c_digit > '9')
        throw std::invalid_argument("c_digit must be in ['0'..'9']");

    std::string sql_text = std::format("UPDATE Games SET C{}=?1 WHERE __id=?2;", c_digit);

    sqlite3_stmt *statement = nullptr;
    SQLFinalize finalize{statement};
    int status = sqlite3_prepare_v2(sql, sql_text.c_str(), -1, &statement, nullptr);
    if (status != SQLITE_OK)
        throw std::runtime_error(std::format("Failed to prepare UPDATE for SQL lobby: {}", sqlite3_errmsg(sql)));

    status = sqlite3_bind_text(statement, 1, value.c_str(), -1, SQLITE_TRANSIENT);
    if (status != SQLITE_OK)
        throw std::runtime_error(std::format("Failed to bind value for SQL lobby: {}", sqlite3_errmsg(sql)));

    status = sqlite3_bind_text(statement, 2, id.c_str(), -1, SQLITE_TRANSIENT);
    if (status != SQLITE_OK)
        throw std::runtime_error(std::format("Failed to bind id for SQL lobby: {}", sqlite3_errmsg(sql)));

    status = sqlite3_step(statement);
    if (status != SQLITE_DONE)
        throw std::runtime_error(std::format("Failed to update game property in SQL lobby: {}", sqlite3_errmsg(sql)));
}

void Lobby::sql_delete_game(const std::string& id) {
    if (!sql)
        throw std::runtime_error("Not an SQL capable lobby!");

    sqlite3_stmt *statement = nullptr;
    SQLFinalize finalize{statement};

    const char *sql_text = "DELETE FROM Games WHERE __id=?1;";
    int status = sqlite3_prepare_v2(sql, sql_text, -1, &statement, nullptr);
    if (status != SQLITE_OK)
        throw std::runtime_error(std::format("Failed to prepare DELETE for SQL lobby: {}", sqlite3_errmsg(sql)));

    status = sqlite3_bind_text(statement, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    if (status != SQLITE_OK)
        throw std::runtime_error(std::format("Failed to bind id for SQL lobby: {}", sqlite3_errmsg(sql)));

    status = sqlite3_step(statement);
    if (status != SQLITE_DONE)
        throw std::runtime_error(std::format("Failed to delete game from SQL lobby: {}", sqlite3_errmsg(sql)));
}
} // namespace server
