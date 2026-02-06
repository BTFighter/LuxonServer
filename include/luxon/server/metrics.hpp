// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <deque>
#include <chrono>

namespace server {
class Metric {
    struct Sample {
        std::chrono::steady_clock::time_point timestamp;
        unsigned value;
    };

    std::deque<Sample> history_;

    template <typename Func> void for_each_in_window(unsigned duration_ms, Func func) {
        if (history_.empty())
            return;
        const auto now = std::chrono::steady_clock::now();

        for (auto it = history_.rbegin(); it != history_.rend(); ++it) {
            auto age_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->timestamp).count();
            if (age_ms > duration_ms)
                break;

            func(it->value);
        }
    }

public:
    void add(unsigned value);
    unsigned min(unsigned duration_ms = 30000);
    unsigned avg(unsigned duration_ms = 30000);
    unsigned max(unsigned duration_ms = 30000);

    template <typename Func> void for_each_metric_in_window(unsigned duration_ms, Func func) {
        if (history_.empty())
            return;
        const auto now = std::chrono::steady_clock::now();

        // Iterate backwards (newest to oldest)
        for (auto it = history_.rbegin(); it != history_.rend(); ++it) {
            auto age_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->timestamp).count();
            if (age_ms > duration_ms)
                break; // Stop if outside the window

            func(it->value);
        }
    }
};
} // namespace server
