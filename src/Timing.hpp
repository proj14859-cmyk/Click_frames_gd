#pragma once
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <iomanip>
#include <istream>
#include <ostream>
#include <string>
#include <vector>

namespace cf {
struct Input { int tick = 0; bool down = false; int button = 1; bool p1 = true; };
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
    std::vector<Input> inputs;
    std::vector<Sample> trace;
    std::vector<Window> windows;
    bool ready() const {
        if (!complete || !verified || windows.size() != inputs.size()) return false;
        for (size_t i = 0; i < inputs.size(); ++i)
            if (inputs[i].button == 1 && !windows[i].measured) return false;
        return true;
    }
};
inline std::string fingerprint(std::string const& content) {
    uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : content) { h ^= c; h *= 1099511628211ULL; }
    return std::to_string(h);
}
inline bool trial(std::vector<Input> const& source, size_t index, int offset, std::vector<Input>& out) {
    if (index >= source.size()) return false;
    out = source; out[index].tick += offset;
    auto e = out[index];
    if (e.tick < 0) return false;
    for (size_t i = 0; i < out.size(); ++i) {
        if (i == index || out[i].p1 != e.p1 || out[i].button != e.button) continue;
        if ((i < index && out[i].tick > e.tick) || (i > index && out[i].tick < e.tick)) return false;
    }
    std::stable_sort(out.begin(), out.end(), [](auto a, auto b) { return a.tick < b.tick; });
    return true;
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
    out << "CLICKFRAMES 1 " << std::quoted(r.key) << ' ' << r.complete << ' ' << r.verified
        << ' ' << r.endTick << ' ' << r.radius << ' ' << r.inputs.size() << ' ' << r.trace.size() << '\n';
    for (size_t i = 0; i < r.inputs.size(); ++i) {
        auto e = r.inputs[i]; auto w = i < r.windows.size() ? r.windows[i] : Window{};
        out << e.tick << ' ' << e.down << ' ' << e.button << ' ' << e.p1 << ' '
            << w.early << ' ' << w.late << ' ' << w.measured << ' ' << w.lowerBound << '\n';
    }
    out << std::setprecision(9);
    for (auto s : r.trace) out << s.tick << ' ' << s.x << ' ' << s.y << ' ' << s.x2 << ' ' << s.y2 << '\n';
}
inline bool read(std::istream& in, std::string const& key, Recording& result) {
    Recording r; std::string magic; int version; size_t count, samples;
    if (!(in >> magic >> version >> std::quoted(r.key) >> r.complete >> r.verified >> r.endTick >> r.radius >> count >> samples)
        || magic != "CLICKFRAMES" || version != 1 || r.key != key || r.radius < 1 || r.radius > 60
        || count > 1000000 || samples > 2000000 || r.endTick < 0) return false;
    int last = -1;
    for (size_t i = 0; i < count; ++i) {
        Input e; Window w;
        if (!(in >> e.tick >> e.down >> e.button >> e.p1 >> w.early >> w.late >> w.measured >> w.lowerBound)
            || e.tick < 0 || e.tick < last || (r.complete && e.tick > r.endTick) || e.button < 1 || e.button > 3 || w.early < 0 || w.late < 0
            || w.early > r.radius || w.late > r.radius) return false;
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
