#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// HUD level time: elapsed physics time over total. Ticks are 240 Hz physics ticks, independent of render FPS.
namespace cf::leveltime {
constexpr double tps = 240.;
constexpr int maxTicks = 24 * 3600 * 240;
constexpr size_t speedCount = 5;

struct Total { int ticks = 0; bool exact = false; };
struct PortalPoint { double x = 0.; int speed = 0; };

inline bool validTicks(long long ticks) { return ticks > 0 && ticks <= maxTicks; }
inline bool validSpeedUnits(double unitsPerTick) {
    return std::isfinite(unitsPerTick) && unitsPerTick > 0.1 && unitsPerTick < 50.;
}

inline int speedIndexFromFactor(double factor) {
    if (!std::isfinite(factor) || factor <= 0.) return -1;
    constexpr double factors[speedCount] = {0.7, 0.9, 1.1, 1.3, 1.6};
    for (size_t i = 0; i < speedCount; ++i) {
        if (std::abs(factor - factors[i]) < 0.05) return static_cast<int>(i);
    }
    return -1;
}

inline int speedIndexFromPortalId(int objectId) {
    switch (objectId) {
        case 200: return 0;
        case 201: return 1;
        case 202: return 2;
        case 203: return 3;
        case 1334: return 4;
        default: return -1;
    }
}

// Segment-by-segment total tick estimate from startX to endX across speed portals.
// Returns nullopt if inputs are invalid or if any active segment lacks a learned speed.
inline std::optional<int> estimateTicks(
    double startX,
    double endX,
    int startSpeed,
    std::vector<PortalPoint> const& puntos,
    std::array<double, speedCount> const& unidadesPorTick
) {
    if (!std::isfinite(startX) || !std::isfinite(endX) || endX <= startX) return std::nullopt;
    if (startSpeed < 0 || static_cast<size_t>(startSpeed) >= speedCount) return std::nullopt;
    if (!validSpeedUnits(unidadesPorTick[startSpeed])) return std::nullopt;

    std::vector<PortalPoint> sorted;
    sorted.reserve(puntos.size());
    for (auto const& p : puntos) {
        if (!std::isfinite(p.x) || p.speed < 0 || static_cast<size_t>(p.speed) >= speedCount) return std::nullopt;
        if (p.x <= startX || p.x >= endX) continue;
        sorted.push_back(p);
    }
    std::stable_sort(sorted.begin(), sorted.end(), [](PortalPoint const& a, PortalPoint const& b) {
        return a.x < b.x;
    });

    double totalTicks = 0.;
    double cursorX = startX;
    int currentSpeed = startSpeed;
    for (auto const& p : sorted) {
        if (p.x > cursorX) {
            double rate = unidadesPorTick[currentSpeed];
            if (!validSpeedUnits(rate)) return std::nullopt;
            totalTicks += (p.x - cursorX) / rate;
            cursorX = p.x;
        }
        currentSpeed = p.speed;
        if (!validSpeedUnits(unidadesPorTick[currentSpeed])) return std::nullopt;
    }
    if (endX > cursorX) {
        double rate = unidadesPorTick[currentSpeed];
        if (!validSpeedUnits(rate)) return std::nullopt;
        totalTicks += (endX - cursorX) / rate;
    }
    if (!std::isfinite(totalTicks)) return std::nullopt;
    auto rounded = std::llround(totalTicks);
    if (!validTicks(rounded)) return std::nullopt;
    return static_cast<int>(rounded);
}

// In-game learner for units/tick per speed index (requires >= 60 clean consecutive samples with < 1% spread).
struct SpeedLearner {
    static constexpr size_t minSamples = 60;
    std::array<std::vector<double>, speedCount> samples{};
    int lastTick = -1;
    double lastX = 0.;
    int lastSpeed = -1;

    void resetAttempt() {
        lastTick = -1;
        lastX = 0.;
        lastSpeed = -1;
    }

    std::optional<std::pair<int, double>> observe(int tick, double x, double speedFactor, bool alive) {
        int speed = speedIndexFromFactor(speedFactor);
        if (!alive || !std::isfinite(x) || speed < 0) {
            resetAttempt();
            return std::nullopt;
        }
        std::optional<std::pair<int, double>> learned;
        if (lastTick >= 0 && tick == lastTick + 1 && speed == lastSpeed) {
            double dx = x - lastX;
            if (validSpeedUnits(dx)) {
                auto& buf = samples[speed];
                if (!buf.empty() && std::abs(dx - buf.back()) > 0.01 * buf.back()) {
                    buf.clear();
                }
                buf.push_back(dx);
                if (buf.size() >= minSamples) {
                    std::vector<double> sorted = buf;
                    std::sort(sorted.begin(), sorted.end());
                    double med = sorted[sorted.size() / 2];
                    if (med > 0. && (sorted.back() - sorted.front()) / med < 0.01) {
                        learned = std::pair<int, double>{speed, med};
                    }
                    buf.clear();
                }
            } else {
                samples[speed].clear();
            }
        }
        lastTick = tick;
        lastX = x;
        lastSpeed = speed;
        return learned;
    }
};

// Priority order:
// 1. Exact saved ticks from a prior clean completion (no "~").
// 2. Portal-segment estimate when speeds are learned ("~"), unless partialStart.
// 3. Progress extrapolation after 5% ("~"), unless partialStart.
// 4. Unknown ("--").
inline std::optional<Total> total(
    long long saved,
    int elapsed,
    double percent,
    bool partialStart,
    std::optional<int> portalEstimate = std::nullopt
) {
    if (validTicks(saved)) return Total{int(saved), true};
    if (partialStart) return std::nullopt;
    if (portalEstimate && validTicks(*portalEstimate)) {
        return Total{std::max(*portalEstimate, std::max(elapsed, 0)), false};
    }
    if (elapsed <= 0 || !std::isfinite(percent) || percent < 5.) return std::nullopt;
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
inline std::string progressClock(std::optional<Total> duration,double percent,int tick,bool partial,bool showTotal,bool decimals){
    auto number=[decimals](double seconds){
        char buffer[48];std::snprintf(buffer,sizeof buffer,decimals?"%.2f":"%.0f",seconds);
        return std::string(buffer);
    };
    if(!duration || !std::isfinite(percent)){
        auto elapsed=partial?std::string("--"):number(std::max(tick,0)/tps);
        return elapsed+(showTotal?" / -- s":" s");
    }
    double end=duration->ticks/tps;
    auto elapsed=number(end*std::clamp(percent,0.,100.)/100.);
    if(showTotal)elapsed+=(duration->exact?" / ":" / ~")+number(end);
    return elapsed+" s";
}

}
