#pragma once
#include "Timing.hpp"
#include "ScanSupport.hpp"
#include <map>
#include <bit>

namespace cf {
// Compare engine observations at the same pre-command physics tick, not at
// render callbacks. Hidden trigger state still requires full continuation probes.
struct Observation {
    std::array<double, 13> values{};
    std::array<int, 10> flags{};
    bool operator==(Observation const&) const = default;
};
struct CausalEvent {
    int tick = 0, kind = 0, object = 0, player = 0, detail = 0;
    bool operator==(CausalEvent const&) const = default;
};
inline bool countsAsCause(int kind) {
    // kind 6 used to contain every gameEventTriggered broadcast. These include
    // contact/material feedback even when no event trigger is listening.
    return kind >= 1 && kind <= 8 && kind != 6;
}
inline std::string describeCause(CausalEvent const& e) {
    constexpr char const* names[] = {"unknown", "orb-call", "trigger-contact", "pad", "teleport", "group-toggle", "raw-signal", "pickup", "event-trigger"};
    std::ostringstream out;
    out << "tick=" << e.tick << " kind=" << e.kind << '(' << (e.kind >= 0 && e.kind <= 8 ? names[e.kind] : "unknown")
        << ") object=" << e.object << " player=" << e.player << " detail=" << e.detail;
    return out.str();
}
inline std::string causalDifference(CausalEvent const* expected, CausalEvent const& observed, size_t index) {
    return "Causal entry " + std::to_string(index) + "\nExpected: " + (expected ? describeCause(*expected) : "<end of reference>") +
        "\nObserved: " + describeCause(observed);
}
inline bool sameCause(CausalEvent const& a, CausalEvent const& b, bool sameTick) {
    return (!sameTick || a.tick == b.tick) && a.kind == b.kind && a.object == b.object &&
        a.player == b.player && a.detail == b.detail;
}
inline int causalTolerance(int offset){return 2+int(std::min<int64_t>(60,std::abs(int64_t(offset))));}
inline bool timedCause(CausalEvent const& a,CausalEvent const& b,int offset){
    return sameCause(a,b,false) && std::abs(int64_t(a.tick)-b.tick)<=causalTolerance(offset);
}
inline bool shiftedLaunch(int reference,int candidate,int offset){return reference>=0 && candidate==reference+offset;}
inline std::string difference(Observation const& a, Observation const& b) {
    constexpr char const* names[] = {"p1.x", "p1.y", "p1.vy", "p2.x", "p2.y", "p2.vy",
        "p1.size", "p1.speed", "p2.size", "p2.speed", "visual.p1.rotation", "visual.p2.rotation", "step.dt"};
    bool dual = a.flags[6] || b.flags[6];
    for (size_t i = 0; i < a.flags.size(); ++i) {
        if (!dual && (i == 1 || i == 3 || i == 5)) continue;
        if (a.flags[i] != b.flags[i]) return "flag[" + std::to_string(i) + "]: " + std::to_string(a.flags[i]) + " -> " + std::to_string(b.flags[i]);
    }
    for (size_t i = 0; i < a.values.size(); ++i) {
        if (i == 10 || i == 11 || (!dual && (i == 3 || i == 4 || i == 5 || i == 8 || i == 9))) continue;
        double tolerance = (i == 0 || i == 1 || i == 3 || i == 4) ? .0001 : 1e-8;
        if (!std::isfinite(a.values[i]) || !std::isfinite(b.values[i]) || std::abs(a.values[i] - b.values[i]) > tolerance) {
            std::ostringstream out; out << std::setprecision(17) << names[i] << ": " << a.values[i] << " -> " << b.values[i]; return out.str();
        }
    }
    return {};
}
inline bool matches(Observation const& a, Observation const& b) { return difference(a, b).empty(); }
// Only a single pending causal event can be amortized. Identity and order are
// checked independently by recordCause; this never hides a changed input/mode.
struct CausalParity {
    int opened = -1;
    size_t recovered = 0;
    bool compare(Observation const& reference, Observation const& observed, int now) {
        int delta = observed.flags[9] - reference.flags[9];
        if (std::abs(delta)>1) return false;
        if (delta && opened<0) opened=now;
        if (opened>=0) {
            if (now-opened>2 || (now-opened==2 && delta)) return false;
            auto a=reference, b=observed; a.flags[9]=b.flags[9];
            for(size_t i : {size_t(0),size_t(1),size_t(3),size_t(4)}) {
                if(i>=3 && !a.flags[6] && !b.flags[6]) continue;
                if(!std::isfinite(a.values[i]) || !std::isfinite(b.values[i]) || std::abs(a.values[i]-b.values[i])>=.05) return false;
                a.values[i]=b.values[i];
            }
            if(!matches(a,b)) return false;
            if(!delta) {opened=-1; ++recovered;}
            return true;
        }
        return matches(reference,observed);
    }
};
inline bool exactRejoin(Observation const& a, Observation const& b) {
    bool dual = a.flags[6] || b.flags[6];
    for (size_t i = 0; i < a.flags.size(); ++i) {
        if (!dual && (i == 1 || i == 3 || i == 5)) continue;
        if (a.flags[i] != b.flags[i]) return false;
    }
    for (size_t i = 0; i < a.values.size(); ++i) {
        if ((!dual && (i == 3 || i == 4 || i == 5 || i == 8 || i == 9 || i == 11))) continue;
        if (!std::isfinite(a.values[i]) || !std::isfinite(b.values[i]) || a.values[i] != b.values[i]) return false;
    }
    return true;
}
inline uint64_t hashWord(uint64_t hash, uint64_t word) {
    for (int i = 0; i < 8; ++i) { hash ^= word & 255; hash *= 1099511628211ull; word >>= 8; }
    return hash;
}
constexpr uint64_t hashStart = 14695981039346656037ull;
inline uint64_t observationHash(Observation const& o) {
    auto hash=hashStart;bool dual=o.flags[6];
    for(size_t i=0;i<o.flags.size();++i) {
        if(!dual && (i==1 || i==3 || i==5))continue;
        hash=hashWord(hash,uint64_t(o.flags[i]));
    }
    for(size_t i=0;i<o.values.size();++i) {
        if(!dual && (i==3 || i==4 || i==5 || i==8 || i==9 || i==11))continue;
        hash=hashWord(hash,std::bit_cast<uint64_t>(o.values[i]==0?0.:o.values[i]));
    }
    return hash;
}
struct RejoinEvidence {
    uint64_t rng = 0, replayRng = 0, sharedRng = 0, causalHash = 0, actorHash = 0, activatedHash = 0;
    bool quietWorld = false;
    bool operator==(RejoinEvidence const&) const = default;
};
inline bool canRejoin(Observation const& reference, Observation const& current,
    RejoinEvidence const& a, RejoinEvidence const& b, int now, int changedUntil) {
    return now > changedUntil && a.quietWorld && b.quietWorld && a == b &&
        observationHash(reference)==observationHash(current) && exactRejoin(reference, current);
}
inline bool candidateTrial(std::vector<Input> const& source, size_t index, int offset, std::vector<Input>& out) {
    if (index >= source.size()) return false;
    auto const& press = source[index];
    bool discrete = press.mode == 0 || press.mode == 2 || press.mode == 6;
    if (!press.down || press.button != 1 || !discrete) return trial(source, index, offset, out);
    auto adjusted = source;
    for (size_t i = index + 1; i < source.size(); ++i) {
        auto const& e = source[i];
        if (e.p1 != press.p1 || e.button != press.button) continue;
        if (e.down || e.mode != press.mode) break;
        int64_t release = int64_t(e.tick) + offset;
        if (release < 0 || release > std::numeric_limits<int>::max()) return false;
        for (size_t j = i + 1; j < source.size(); ++j)
            if (source[j].p1 == e.p1 && source[j].button == e.button && release > source[j].tick) return false;
        adjusted[i].tick = int(release);
        break;
    }
    return trial(adjusted, index, offset, out);
}
inline bool recoveryTrial(std::vector<Input> const& source, size_t target, int offset, size_t neighbor, int change, std::vector<Input>& out) {
    if (target >= source.size() || neighbor <= target || neighbor >= source.size() ||
        source[target].p1 != source[neighbor].p1) return false;
    std::vector<Input> first;
    if (!trial(source, neighbor, change, first)) return false;
    auto found = std::find_if(first.begin(), first.end(), [target](auto const& e) { return e.sourceIndex == target; });
    if (found == first.end() || !candidateTrial(first, size_t(found - first.begin()), offset, out)) return false;
    for (auto& e : out) e.sourceIndex = first[e.sourceIndex].sourceIndex;
    return true;
}
inline bool allPressesDispatched(std::vector<Input> const& events, size_t next) {
    return next <= events.size() && std::none_of(events.begin() + next, events.end(), [](auto const& e) { return e.down; });
}
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
struct PhysicsReference {
    Sample point;
    int mode1 = 0, mode2 = 0;
    bool flipped1 = false, flipped2 = false;
};
struct SegmentExit {
    bool known = false;
    int tick = 0;
    float x = 0, y = 0;
    int mode = 0;
    bool flipped = false;
};
template<class Reference>
inline std::vector<SegmentExit> segmentExits(Recording const& r, Reference const& reference) {
    std::vector<SegmentExit> exits(r.inputs.size()); std::array<int, 2> next{r.endTick, r.endTick};
    for (size_t i = r.inputs.size(); i-- > 0;) {
        auto const& e = r.inputs[i]; int player = e.p1 ? 0 : 1;
        auto found = referenceAt(reference, next[player]);
        if (next[player] > e.tick && found) {
            auto const& p = *found;
            exits[i] = {true, next[player], e.p1 ? p.point.x : p.point.x2, e.p1 ? p.point.y : p.point.y2,
                e.p1 ? p.mode1 : p.mode2, e.p1 ? p.flipped1 : p.flipped2};
        }
        if (counted(e)) next[player] = e.tick;
    }
    return exits;
}
inline bool atSegmentExit(SegmentExit const& exit, float x, float y, int mode, bool flipped) {
    return exit.known && exit.mode == mode && exit.flipped == flipped &&
        std::abs(exit.x - x) <= 60.f && std::abs(exit.y - y) <= 30.f;
}
template<class State>
inline int snapshotBefore(std::vector<State> const& states, int earliest) {
    int best = -1;
    for (size_t i = 0; i < states.size(); ++i)
        if (!states[i].rejected && states[i].tick < earliest && (best < 0 || states[i].tick > states[best].tick)) best = int(i);
    return best;
}
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
