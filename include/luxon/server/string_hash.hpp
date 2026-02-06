// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <string_view>
#include <cstdint>

namespace server {
// FNV-1a (64-bit), non-secure
[[nodiscard]]
constexpr std::uint64_t string_hash(std::string_view sv) noexcept {
    std::uint64_t h; // offset basis
    if constexpr (sizeof(size_t) == 4)
        h = 2166136261u;
    else
        h = 14695981039346656037ull;
    for (unsigned char c : sv) {
        h ^= c;
        h *= 1099511628211ull; // FNV prime
    }
    return h;
}

struct StringPairHasher {
    std::size_t operator()(const std::pair<std::string_view, std::string_view>& p) const {
        const std::size_t h1 = string_hash(p.first);
        const std::size_t h2 = string_hash(p.second);
        return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
    }
};
} // namespace server
