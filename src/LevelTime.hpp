#pragma once
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>

// HUD level time: elapsed physics time over total. Ticks are 240 Hz physics ticks, independent of render FPS.
namespace cf::leveltime {
constexpr double tps = 240.;
constexpr int maxTicks = 24 * 3600 * 240;
struct Total { int ticks = 0; bool exact = false; };

inline bool validTicks(long long ticks) { return ticks > 0 && ticks <= maxTicks; }

// Exact when this level was finished before (saved ticks). Otherwise a progress extrapolation, only after 5%
// and never from a start position, test mode or platformer, where elapsed time and percent do not line up.
// shortcut: extrapolation assumes constant speed, so it drifts across speed portals; it is shown with "~".
inline std::optional<Total> total(long long saved, int elapsed, double percent, bool partialStart) {
    if (validTicks(saved)) return Total{int(saved), true};
    if (partialStart || elapsed <= 0 || !std::isfinite(percent) || percent < 5.) return std::nullopt;
    auto estimate = std::llround(elapsed * 100. / std::min(percent, 100.));
    if (!validTicks(estimate)) return std::nullopt;
    return Total{int(std::max<long long>(estimate, elapsed)), false};
}

inline std::string seconds(double value) {
    char buffer[32]; std::snprintf(buffer, sizeof buffer, "%.2f s", value); return buffer;
}
inline std::string format(int elapsed, std::optional<Total> total) {
    auto text = seconds(elapsed / tps);
    if (!total) return text + " / --";
    return text + " / " + (total->exact ? "" : "~") + seconds(total->ticks / tps);
}
}
