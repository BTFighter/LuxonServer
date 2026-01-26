#include "metrics.hpp"

namespace server {
void Metric::add(unsigned value) {
    const auto now = std::chrono::steady_clock::now();
    history_.push_back({now, value});

    while (!history_.empty()) {
        auto age_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - history_.front().timestamp).count();
        if (age_ms > 60000)
            history_.pop_front();
        else
            break;
    }
}

unsigned Metric::min(unsigned duration_ms) {
    unsigned min_val = UINT32_MAX;
    bool found = false;

    for_each_in_window(duration_ms, [&](unsigned val) {
        if (val < min_val)
            min_val = val;
        found = true;
    });

    return found ? min_val : 0;
}

unsigned Metric::max(unsigned duration_ms) {
    unsigned max_val = 0;

    for_each_in_window(duration_ms, [&](unsigned val) {
        if (val > max_val)
            max_val = val;
    });

    return max_val;
}

unsigned Metric::avg(unsigned duration_ms) {
    uint64_t total = 0;
    size_t count = 0;

    for_each_in_window(duration_ms, [&](unsigned val) {
        total += val;
        count++;
    });

    return count > 0 ? static_cast<unsigned>(total / count) : 0;
}
} // namespace server
