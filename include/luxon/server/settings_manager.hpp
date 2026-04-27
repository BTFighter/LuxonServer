// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "apps.hpp"
#include "authentication.hpp"
#include "sqlite3pp.hpp"

#include <string>
#include <optional>
#include <filesystem>
#include <cstdint>
#include <memory>

namespace server {
class SettingsManager {
public:
    /// Constructs the SettingsManager, checks writability, and runs migrations if writable
    SettingsManager(const std::filesystem::path& db_path);
    ~SettingsManager() = default;

    // Delete copy/move constructors as we manage a database connection
    SettingsManager(const SettingsManager&) = delete;
    SettingsManager& operator=(const SettingsManager&) = delete;

    /// Retrieves and caps app settings for the given appid
    std::optional<AppSettings> get_app_settings(const std::string& appid);

    /// Retrieves specific authentication provider settings
    std::optional<AuthProviderSettings> get_auth_provider(const std::string& appid, uint8_t auth_type);

private:
    void apply_migrations();
    bool check_is_writable(const std::filesystem::path& db_path) const;

    std::unique_ptr<sqlite3pp::database> db_;
    bool is_read_only_;
};
} // namespace server
