#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <iomanip>
#include <limits>
#include <istream>
#include <iterator>
#include <map>
#include <ostream>
#include <string>
#include <vector>

namespace cf {
struct Input {
    int tick = 0;
    bool down = false;
    int button = 1;
    bool p1 = true;
    float x = 0.f, y = 0.f;
    int mode = -1; // legacy unknown; 0 cube, 1 ship, 2 ball, 3 UFO, 4 wave, 5 robot, 6 spider, 7 swing
    size_t sourceIndex = size_t(-1); // transient identity while a candidate is sorted
};
inline bool counted(Input const& e) {
    return e.button == 1 && (e.down || e.mode == 1 || e.mode == 4 || e.mode == 5 || e.mode == 7);
}
struct Sample { int tick; float x, y, x2, y2; };
struct Window {
    int early = 0, late = 0;
    bool measured = false, lowerBound = false;
    int width() const { return early + late + 1; }
};
struct Recording {
    std::string key;
    bool complete = false, verified = false;
    int endTick = 0, radius = 12;
    int analysisPolicy = 0;
    std::string backend = "vanilla240";
    std::vector<Input> inputs;
    std::vector<Sample> trace;
    std::vector<Window> windows;
    bool ready() const {
        if (!complete || !verified || windows.size() != inputs.size()) return false;
        for (size_t i = 0; i < inputs.size(); ++i)
            if (counted(inputs[i]) && !windows[i].measured) return false;
        return true;
    }
};
// Counter rows span every width from the smallest measured value to the largest,
// including empty intermediate widths. The last row groups wider values when capped.
struct Row { int from, to; bool open; }; // widths from..to; open = shown as "from+"
inline std::vector<Row> makeRows(Recording const& r, int maxRows) {
    int lo = 1 << 30, hi = 0; bool openTop = false;
    for (size_t i = 0; i < r.inputs.size() && i < r.windows.size(); ++i) {
        if (!counted(r.inputs[i]) || !r.windows[i].measured) continue;
        int w = r.windows[i].width();
        lo = std::min(lo, w);
        if (w > hi) { hi = w; openTop = false; }
        if (w == hi && r.windows[i].lowerBound) openTop = true;
    }
    std::vector<Row> rows;
    if (hi == 0) return rows;
    int limit = std::max(2, maxRows);
    for (int w = lo; w <= hi; ++w) {
        if (static_cast<int>(rows.size()) + 1 == limit && w < hi) { rows.push_back({w, hi, true}); break; }
        rows.push_back({w, w, w == hi && openTop});
    }
    return rows;
}
inline int rowOf(std::vector<Row> const& rows, Window const& w) {
    for (size_t i = 0; i < rows.size(); ++i)
        if (w.width() >= rows[i].from && w.width() <= rows[i].to) return static_cast<int>(i);
    return -1;
}
inline std::string label(Row const& row) { return std::to_string(row.from) + (row.open ? "+" : ""); }
struct Rgb { unsigned char r, g, b; };
// Tight is red, loose is green: width 1 = red ... 10 or more = green.
inline Rgb colorFor(int width) {
    static constexpr Rgb stops[5] = {{255, 75, 75}, {255, 150, 55}, {255, 225, 70}, {245, 245, 245}, {105, 245, 125}};
    float t = std::clamp((width - 1) / 9.f, 0.f, 1.f) * 4.f;
    int i = std::min(3, static_cast<int>(t));
    float f = t - static_cast<float>(i);
    auto mix = [f](unsigned char a, unsigned char b) { return static_cast<unsigned char>(a + (b - a) * f + .5f); };
    return {mix(stops[i].r, stops[i + 1].r), mix(stops[i].g, stops[i + 1].g), mix(stops[i].b, stops[i + 1].b)};
}
inline std::string fingerprint(std::string const& content) {
    uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : content) { h ^= c; h *= 1099511628211ULL; }
    return std::to_string(h);
}
inline bool trial(std::vector<Input> const& source, size_t index, int offset, std::vector<Input>& out) {
    if (index >= source.size()) return false;
    out = source;
    for (size_t i = 0; i < out.size(); ++i) out[i].sourceIndex = i;
    auto shiftedTick = static_cast<int64_t>(out[index].tick) + offset;
    if (shiftedTick < 0 || shiftedTick > std::numeric_limits<int>::max()) return false;
    out[index].tick = static_cast<int>(shiftedTick);
    auto e = out[index];
    if (e.tick < 0) return false;
    for (size_t i = 0; i < out.size(); ++i) {
        if (i == index || out[i].p1 != e.p1 || out[i].button != e.button) continue;
        if ((i < index && out[i].tick > e.tick) || (i > index && out[i].tick < e.tick)) return false;
    }
    std::stable_sort(out.begin(), out.end(), [](auto a, auto b) { return a.tick < b.tick; });
    return true;
}
// Whole-run shift (a copy, so source and out may be the same vector); false if a tick would go below 0.
inline bool shiftAll(std::vector<Input> const& source, int by, std::vector<Input>& out) {
    auto shifted = source;
    for (auto& e : shifted) {
        auto tick = static_cast<int64_t>(e.tick) + by;
        if (tick < 0 || tick > std::numeric_limits<int>::max()) return false;
        e.tick = static_cast<int>(tick);
    }
    out = std::move(shifted); return true;
}
inline Window measure(std::vector<bool> const& success, int radius) {
    Window w;
    if (success.size() != size_t(2 * radius + 1) || !success[radius]) return w;
    while (w.early < radius && success[radius - w.early - 1]) ++w.early;
    while (w.late < radius && success[radius + w.late + 1]) ++w.late;
    w.measured = true; w.lowerBound = w.early == radius || w.late == radius;
    return w;
}
inline void write(std::ostream& out, Recording const& r) {
    out << "CLICKFRAMES 4 " << std::quoted(r.key) << ' ' << r.complete << ' ' << r.verified
        << ' ' << r.endTick << ' ' << r.radius << ' ' << r.inputs.size() << ' ' << r.trace.size()
        << ' ' << r.analysisPolicy << ' ' << std::quoted(r.backend) << '\n';
    out << std::setprecision(9);
    for (size_t i = 0; i < r.inputs.size(); ++i) {
        auto e = r.inputs[i]; auto w = i < r.windows.size() ? r.windows[i] : Window{};
        out << e.tick << ' ' << e.down << ' ' << e.button << ' ' << e.p1 << ' '
            << e.x << ' ' << e.y << ' ' << e.mode << ' '
            << w.early << ' ' << w.late << ' ' << w.measured << ' ' << w.lowerBound << '\n';
    }
    for (auto s : r.trace) out << s.tick << ' ' << s.x << ' ' << s.y << ' ' << s.x2 << ' ' << s.y2 << '\n';
}
inline bool read(std::istream& in, std::string const& key, Recording& result) {
    Recording r; std::string magic; int version; size_t count, samples;
    if (!(in >> magic >> version >> std::quoted(r.key) >> r.complete >> r.verified >> r.endTick >> r.radius >> count >> samples)
        || magic != "CLICKFRAMES" || (version != 2 && version != 3 && version != 4) || r.key != key || r.radius < 1 || r.radius > 60
        || count > 1000000 || samples > 2000000 || r.endTick < 0) return false;
    if (version == 4 && (!(in >> r.analysisPolicy >> std::quoted(r.backend)) || r.analysisPolicy < 0 || r.analysisPolicy > 1 ||
        (r.backend != "vanilla240" && r.backend != "cos240"))) return false;
    int last = -1;
    for (size_t i = 0; i < count; ++i) {
        Input e; Window w;
        if (!(in >> e.tick >> e.down >> e.button >> e.p1 >> e.x >> e.y)) return false;
        if (version >= 3 && (!(in >> e.mode) || e.mode < -1 || e.mode > 7)) return false;
        if (!(in >> w.early >> w.late >> w.measured >> w.lowerBound)
            || e.tick < 0 || e.tick < last || (r.complete && e.tick > r.endTick) || e.button < 1 || e.button > 3 || w.early < 0 || w.late < 0
            || w.early > r.radius || w.late > r.radius || !std::isfinite(e.x) || !std::isfinite(e.y)) return false;
        last = e.tick; r.inputs.push_back(e); r.windows.push_back(w);
    }
    last = -1;
    for (size_t i = 0; i < samples; ++i) {
        Sample s;
        if (!(in >> s.tick >> s.x >> s.y >> s.x2 >> s.y2) || s.tick < 0 || s.tick < last ||
            !std::isfinite(s.x) || !std::isfinite(s.y) || !std::isfinite(s.x2) || !std::isfinite(s.y2)) return false;
        last = s.tick; r.trace.push_back(s);
    }
    result = std::move(r); return true;
}
}
