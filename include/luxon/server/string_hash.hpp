// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <string_view>
#include <cstdint>

namespace server {
// FNV-1a (64-bit), non-secure
[[nodiscard]]
constexpr std::size_t string_hash(std::string_view s) noexcept {
    if constexpr (sizeof(std::size_t) == 8) {
        // 64-bit FNV-1a
        std::size_t h = 1469598103934665603ULL;
        for (uint8_t b : s) {
            h ^= b;
            h *= 1099511628211ULL;
        }
        return h;
    } else {
        // 32-bit FNV-1a
        // Basis: 2166136261, Prime: 16777619
        std::size_t h = 2166136261U;
        for (uint8_t b : s) {
            h ^= b;
            h *= 16777619U;
        }
        return h;
    }
}

struct StringPairHasher {
    std::size_t operator()(const std::pair<std::string_view, std::string_view>& p) const {
        const std::size_t h1 = string_hash(p.first);
        const std::size_t h2 = string_hash(p.second);
        return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
    }
};
} // namespace server
