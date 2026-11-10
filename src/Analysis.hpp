#pragma once
#include "Timing.hpp"
#include <map>

namespace cf {
// Baseline offset zero is known to pass. Visit only the connected component.
struct ContinuousSearch {
    int radius = 1, offset = -1;
    bool done = false;
    std::vector<bool> outcomes;
    void reset(int r) {
        radius = std::clamp(r, 1, 60); offset = -1; done = false;
        outcomes.assign(2 * radius + 1, false); outcomes[radius] = true;
    }
    void accept(bool pass) {
        if (done) return;
        outcomes[offset + radius] = pass;
        if (offset < 0) {
            if (!pass || offset == -radius) offset = 1;
            else --offset;
        } else if (!pass || offset == radius) done = true;
        else ++offset;
    }
    Window window() const { return measure(outcomes, radius); }
};
struct Landing {
    bool known = false;
    int tick = 0;
    uintptr_t platform = 0;
    float x = 0, y = 0;
};
inline bool sameLanding(Landing const& reference, Landing const& candidate, float corridor) {
    return reference.known && candidate.known && reference.platform == candidate.platform &&
        std::abs(reference.x - candidate.x) <= corridor && std::abs(reference.y - candidate.y) <= 6.f;
}
struct Prediction {
    int offset = 0, endTick = 0;
    bool pass = false;
    std::vector<Sample> points;
};
using Predictions = std::map<size_t, std::vector<Prediction>>;
constexpr size_t MAX_PREDICTION_POINTS = 32768;
inline void writePredictions(std::ostream& out, std::string const& digest, Predictions const& all) {
    size_t count = 0;
    for (auto const& [event, branches] : all) for (auto const& branch : branches) count += branch.points.size();
    out << "CFPRED 1 " << digest << ' ' << count << '\n' << std::setprecision(9);
    for (auto const& [event, branches] : all) for (auto const& b : branches)
        for (auto const& p : b.points)
            out << event << ' ' << b.offset << ' ' << b.pass << ' ' << b.endTick << ' '
                << p.tick << ' ' << p.x << ' ' << p.y << ' ' << p.x2 << ' ' << p.y2 << '\n';
}
inline bool readPredictions(std::istream& in, std::string const& digest, size_t inputs, Predictions& result) {
    std::string magic, hash; int version; size_t count;
    if (!(in >> magic >> version >> hash >> count) || magic != "CFPRED" || version != 1 || hash != digest || count > MAX_PREDICTION_POINTS) return false;
    Predictions parsed;
    for (size_t i = 0; i < count; ++i) {
        size_t event; int offset, end; bool pass; Sample p;
        if (!(in >> event >> offset >> pass >> end >> p.tick >> p.x >> p.y >> p.x2 >> p.y2) || event >= inputs ||
            offset < -60 || offset > 60 || end < 0 || p.tick < 0 || p.tick > end ||
            !std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.x2) || !std::isfinite(p.y2)) return false;
        auto& branches = parsed[event];
        auto it = std::find_if(branches.begin(), branches.end(), [offset](auto const& b) { return b.offset == offset; });
        if (it == branches.end()) {
            if (branches.size() >= 3) return false;
            branches.push_back({offset, end, pass, {}}); it = std::prev(branches.end());
        }
        if (it->pass != pass || it->endTick != end || (!it->points.empty() && p.tick <= it->points.back().tick)) return false;
        if (it->points.size() >= 256) return false;
        it->points.push_back(p);
    }
    result = std::move(parsed); return true;
}
}
