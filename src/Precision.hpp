#pragma once
// Statistical model adapted from hyper-5/frame-window-counter (MIT, 2026).
// See licenses/frame-window-counter.MIT.txt. No GD/Geode dependencies.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace cf::precision {
inline double windowMs(int frames,double tps=240.) {
    return frames>0 && std::isfinite(tps) && tps>0?double(frames)*1000./tps:0.;
}
constexpr double minL = .001, maxL = 1e15, unresolved = 1e100;
constexpr int secantIterations = 10, bisectionIterations = 60;
enum class Weighting { Base, N, F, C, NF, NC, FC, NFC };
constexpr std::array<char const*, 8> names{"Base", "Nerve", "Fatigue", "CPS", "N+F", "N+C", "F+C", "N+F+C"};
struct Params {
    double fps = 240, respawn = 0, target = 86400;
    double kT = .0016520833717346, kU = .0002727763242154, kC = .2784421686721826;
    bool operator==(Params const&) const = default;
};
struct Click {
    double time = 0;
    int64_t index = 1;
    double windowFrames = 1;
    int subInputs = 1;
    bool lowerBound = false;
};
struct Metric {
    double lstar = 0;
    bool valid = false;
    double sigmaMs() const { return valid && lstar > 0 ? 1000. / lstar : 0; }
};
struct Prefix { double time = 0; bool conservative = false; std::array<Metric, 8> metrics{}; };
struct Result { std::vector<Prefix> prefixes; std::string error; bool cancelled = false; };
using Cancel = std::function<bool()>;
inline bool stopped(Cancel const& cancel) { return cancel && cancel(); }

inline double erfcFast(double x) {
    static const auto lut = [] {
        std::vector<double> values(100001); // Keep ~800 KiB off Android worker stacks.
        for (size_t i = 0; i < values.size(); ++i) values[i] = std::erfc(double(i) * 6. / 100000.);
        return values;
    }();
    if (std::isnan(x)) return std::numeric_limits<double>::quiet_NaN();
    if (x <= 0) return 1;
    if (x >= 6) return 0;
    double location = x * (100000. / 6.);
    size_t i = size_t(location);
    return lut[i] + (location - double(i)) * (lut[i + 1] - lut[i]);
}
struct Prepared {
    std::vector<double> times;
    std::array<std::vector<double>, 8> weights;
    std::vector<size_t> ends;
    std::string error;
};
inline bool valid(Params const& p) {
    return std::isfinite(p.fps) && p.fps > 0 && std::isfinite(p.respawn) && p.respawn >= 0 &&
        std::isfinite(p.target) && p.target > 0 && std::isfinite(p.kT) && p.kT >= 0 &&
        std::isfinite(p.kU) && p.kU >= 0 && std::isfinite(p.kC) && p.kC >= 0;
}
inline Prepared prepare(std::span<Click const> clicks, Params const& p, Cancel const& cancel = {}) {
    Prepared out;
    if (!valid(p)) { out.error = "Invalid precision parameters"; return out; }
    double previous = 0;
    int64_t previousIndex = 0, additionalInputs = 0;
    for (auto const& click : clicks) {
        if (stopped(cancel)) { out.error = "Cancelled"; return out; }
        if (!std::isfinite(click.time) || click.time < 0 || !std::isfinite(click.windowFrames) || click.windowFrames <= 0 ||
            click.index < 1 || click.subInputs < 1 || click.subInputs > 1000000 || out.times.size() + size_t(click.subInputs) > 1000000) {
            out.error = "Invalid or oversized precision input"; return out;
        }
        if (click.index > std::numeric_limits<int64_t>::max() - additionalInputs - (click.subInputs - 1)) {
            out.error = "Precision input index overflow"; return out;
        }
        for (int s = 0; s < click.subInputs; ++s) {
            double t = p.respawn + click.time + double(s) / (click.subInputs * p.fps);
            if (!std::isfinite(t) || (!out.times.empty() && t < previous)) { out.error = "Precision inputs are not chronological"; return out; }
            int64_t input = click.index + additionalInputs + s;
            if (input <= previousIndex) { out.error = "Precision input indices are not increasing"; return out; }
            double w = click.subInputs == 1 ? click.windowFrames : s == 0 ?
                std::max(click.windowFrames - (click.subInputs - 1.) / click.subInputs, 1. / click.subInputs) : 1. / click.subInputs;
            double n = std::exp(-p.kT * t), f = std::exp(-p.kU * double(input));
            double delta = (t - previous) / double(input - previousIndex);
            if (delta <= 0) delta = 1; // Matches the upstream same-frame convention.
            double c = std::pow(4. / std::max(1., 2. / delta), p.kC);
            double base = w / p.fps * .5 * .7071067811865475;
            std::array<double, 8> weights{base, base*n, base*f, base*c, base*n*f, base*n*c, base*f*c, base*n*f*c};
            for (size_t k = 0; k < 8; ++k) {
                if (!std::isfinite(weights[k]) || weights[k] < 0) { out.error = "Precision weight overflow"; return out; }
                out.weights[k].push_back(weights[k]);
            }
            out.times.push_back(t); previous = t; previousIndex = input;
        }
        additionalInputs += click.subInputs - 1;
        out.ends.push_back(out.times.size());
    }
    return out;
}
inline double expectedClear(std::span<double const> times, std::span<double const> weights, double L, Cancel const& cancel = {}) {
    if (times.empty() || times.size() != weights.size() || !std::isfinite(L) || L <= 0) return unresolved;
    double survival = 1, failedTime = 0;
    for (size_t j = 0; j < times.size(); ++j) {
        if ((j & 255) == 0 && stopped(cancel)) return unresolved;
        double x = weights[j] * L;
        if (!std::isfinite(times[j]) || times[j] < 0 || !std::isfinite(weights[j]) || weights[j] < 0) return unresolved;
        double q = x > 6 ? 0 : erfcFast(x);
        double probability = std::max(1e-15, 1. - q);
        failedTime += times[j] * survival * q;
        survival *= probability;
        if (survival < 1e-200) return unresolved;
    }
    double estimate = times.back() + failedTime / survival;
    return std::isfinite(estimate) ? std::min(estimate, unresolved) : unresolved;
}
inline Metric solvePrepared(std::span<double const> times, std::span<double const> weights, Params const& p,
    double guess = minL, Cancel const& cancel = {}) {
    if (!valid(p) || times.empty() || times.back() <= 0 || times.back() >= p.target || stopped(cancel)) return {};
    double left = minL, right = std::clamp(std::isfinite(guess) ? guess + 10 : 10, minL * 2, maxL);
    double fLeft = expectedClear(times, weights, left, cancel) - p.target;
    if (fLeft <= 0) return {left, true};
    if (std::isfinite(guess) && guess > minL && guess < maxL) {
        double fGuess = expectedClear(times, weights, guess, cancel) - p.target;
        if (std::abs(fGuess) <= 1e-8 * p.target) return {guess, true};
        if (fGuess > 0) {
            // Verify the warm-start bracket instead of assuming prefix monotonicity.
            left = guess; fLeft = fGuess;
            right = std::min(maxL, guess * 1.001 + .001);
        } else right = std::min(guess, 10.); // Avoid a huge arbitrary guess exhausting 60 bisections near minL.
    }
    double fRight = expectedClear(times, weights, right, cancel) - p.target;
    for (int i = 0; fRight > 0 && i < 64 && right < maxL; ++i) {
        if (stopped(cancel)) return {};
        right = std::min(maxL, right * 2);
        fRight = expectedClear(times, weights, right, cancel) - p.target;
    }
    if (fRight > 0 || stopped(cancel)) return {};
    // Safeguarded secant steps; fall back to bisection inside a valid bracket.
    for (int iteration = 0; iteration < secantIterations + bisectionIterations; ++iteration) {
        if (stopped(cancel)) return {};
        double mid = (left + right) * .5;
        if (iteration < secantIterations && std::abs(fLeft) < 1e90 && std::abs(fRight) < 1e90 && fRight != fLeft) {
            double secant = right - fRight * (right - left) / (fRight - fLeft);
            if (std::isfinite(secant) && secant > left && secant < right) mid = secant;
        }
        double f = expectedClear(times, weights, mid, cancel) - p.target;
        if (std::abs(f) <= 1e-8 * p.target) return {mid, true};
        if (f > 0) { left = mid; fLeft = f; } else { right = mid; fRight = f; }
        if (right - left < 1e-7 * std::max(1., mid)) break;
    }
    return {(left + right) * .5, true};
}
inline Metric solveL(std::span<Click const> clicks, Weighting weighting, Params const& p, double guess = minL, Cancel const& cancel = {}) {
    auto prepared = prepare(clicks, p, cancel);
    if (!prepared.error.empty() || size_t(weighting) >= 8) return {};
    return solvePrepared(prepared.times, prepared.weights[size_t(weighting)], p, guess, cancel);
}
inline Result calculate(std::span<Click const> clicks, Params const& p, Cancel const& cancel = {}) {
    Result result;
    auto prepared = prepare(clicks, p, cancel);
    if (!prepared.error.empty()) { result.error = prepared.error; result.cancelled = stopped(cancel); return result; }
    std::array<double, 8> guesses{}; guesses.fill(minL);
    bool conservative = false;
    for (size_t i = 0; i < clicks.size(); ++i) {
        if (stopped(cancel)) { result.cancelled = true; result.prefixes.clear(); return result; }
        conservative = conservative || clicks[i].lowerBound;
        Prefix prefix; prefix.time = clicks[i].time; prefix.conservative = conservative;
        for (size_t k = 0; k < 8; ++k) {
            prefix.metrics[k] = solvePrepared(std::span(prepared.times).first(prepared.ends[i]),
                std::span(prepared.weights[k]).first(prepared.ends[i]), p, guesses[k], cancel);
            if (prefix.metrics[k].valid) guesses[k] = prefix.metrics[k].lstar;
        }
        result.prefixes.push_back(prefix);
    }
    if (stopped(cancel)) { result.cancelled = true; result.prefixes.clear(); }
    return result;
}
}
