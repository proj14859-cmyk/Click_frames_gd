#pragma once
#include "Timing.hpp"
#include <optional>
#include <span>
#include <compare>

namespace cf {
template<class T> class TickTable {
    std::vector<std::optional<T>> entries;
    size_t count = 0;
public:
    static constexpr int limit = 250000;
    bool insert(int tick, T value) {
        if (tick < 0 || tick >= limit) return false;
        if (size_t(tick) >= entries.size()) entries.resize(size_t(tick) + 1);
        if (entries[tick]) return false;
        entries[tick] = std::move(value); ++count; return true;
    }
    void set(int tick, T value) {
        if (tick < 0 || tick >= limit) return;
        if (size_t(tick) >= entries.size()) entries.resize(size_t(tick) + 1);
        if (!entries[tick]) ++count;
        entries[tick] = std::move(value);
    }
    T const* get(int tick) const {
        return tick >= 0 && size_t(tick) < entries.size() && entries[tick] ? &*entries[tick] : nullptr;
    }
    bool contains(int tick) const { return get(tick) != nullptr; }
    size_t size() const { return count; }
    void clear() { entries.clear(); count = 0; }
    void release() { std::vector<std::optional<T>>{}.swap(entries); count = 0; }
};
template<class T> T const* referenceAt(TickTable<T> const& table, int tick) { return table.get(tick); }
template<class T> T const* referenceAt(std::map<int, T> const& table, int tick) {
    auto found = table.find(tick); return found == table.end() ? nullptr : &found->second;
}

// Never skip an untested offset. Widen the initial radius four in stages;
// optionally stop with a proven lower bound once enough valid ticks are known.
struct AdaptiveSearch {
    int radius = 1, stage = 1, offset = -1, early = 0, late = 0, broadLimit = 0;
    bool done = false, leftClosed = false, rightClosed = false;
    void reset(int r, int cap = 0) {
        radius = std::clamp(r, 1, 60); stage = std::min(4, radius); offset = -1;
        early = late = 0; broadLimit = std::max(0, cap); done = leftClosed = rightClosed = false;
    }
    void accept(bool pass) {
        if (done) return;
        if (offset < 0) { if (pass) ++early; else leftClosed = true; }
        else { if (pass) ++late; else rightClosed = true; }
        if (broadLimit > 0 && early + late + 1 >= broadLimit && !(leftClosed && rightClosed)) { done = true; return; }
        for (;;) {
            if (!leftClosed && early < stage) { offset = -early - 1; return; }
            if (!rightClosed && late < stage) { offset = late + 1; return; }
            if ((leftClosed && rightClosed) || stage == radius) { done = true; return; }
            stage = std::min(radius, stage * 2);
        }
    }
    Window window() const { return {early, late, true, !leftClosed || !rightClosed}; }
};
inline std::vector<int> checkpointTargets(std::span<Input const> inputs, int radius, size_t budget) {
    std::vector<int> targets;
    for (auto const& e : inputs) if (counted(e) && e.tick > radius + 2) targets.push_back(e.tick - radius - 2);
    std::sort(targets.begin(), targets.end()); targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
    if (!budget) return {};
    if (targets.size() <= budget) return targets;
    std::vector<int> sampled; sampled.reserve(budget);
    for (size_t i = 0; i < budget; ++i) sampled.push_back(targets[budget == 1 ? 0 : i * (targets.size() - 1) / (budget - 1)]);
    return sampled;
}
inline int lastChangedTick(std::span<Input const> original, std::span<Input const> candidate) {
    int last = -1;
    for (size_t i = 0; i < candidate.size(); ++i) {
        auto const& e = candidate[i]; size_t source = e.sourceIndex == size_t(-1) ? i : e.sourceIndex;
        if (source >= original.size()) return std::numeric_limits<int>::max();
        if (e.tick != original[source].tick || e.down != original[source].down || e.button != original[source].button || e.p1 != original[source].p1)
            last = std::max(last, std::max(e.tick, original[source].tick));
    }
    return original.size() == candidate.size() ? last : std::numeric_limits<int>::max();
}
struct ProbeKey {
    size_t event = 0; int offset = 0; bool full = false;
    auto operator<=>(ProbeKey const&) const = default;
};
struct ProbeResult { bool pass = false, fullStart = false; };
class ProbeCache {
    std::map<ProbeKey, ProbeResult> values;
public:
    void clear() { values.clear(); }
    void put(ProbeKey key, ProbeResult result) { if (values.size() < 100000) values[key] = result; }
    ProbeResult const* get(ProbeKey key, bool requireFullStart = false) const {
        auto found = values.find(key);
        return found != values.end() && (!requireFullStart || found->second.fullStart) ? &found->second : nullptr;
    }
};
struct AuditGate {
    size_t earlyPasses = 0, audits = 0, failures = 0;
    bool disabled = false;
    bool consider() { return !disabled && (++earlyPasses % 10 == 0); }
    void observed(bool pass) { ++audits; if (!pass) { ++failures; disabled = true; } }
};
}
