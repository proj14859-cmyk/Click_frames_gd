#include "../src/Analysis.hpp"
#include <cassert>
#include <iostream>
int main() {
    cf::TickTable<int> ticks;
    assert(!ticks.get(0) && !ticks.get(-1));
    assert(ticks.insert(8, 42) && ticks.size() == 1 && *ticks.get(8) == 42);
    assert(!ticks.insert(8, 9) && *ticks.get(8) == 42 && !ticks.get(7));
    assert(!ticks.insert(cf::TickTable<int>::limit, 1));
    ticks.set(8, 99); assert(*ticks.get(8) == 99 && ticks.size() == 1);
    ticks.clear(); assert(!ticks.get(8) && ticks.size() == 0);
    for (int radius = 1; radius <= 7; ++radius) {
        for (unsigned mask = 0; mask < (1u << (2*radius)); ++mask) {
            std::vector<bool> truth(2*radius+1); truth[radius] = true;
            int bit = 0;
            for (int i = 0; i <= 2*radius; ++i) if (i != radius) truth[i] = (mask >> bit++) & 1;
            cf::AdaptiveSearch search; search.reset(radius); std::vector<int> visited; int count = 0;
            while (!search.done) {
                assert(std::find(visited.begin(), visited.end(), search.offset) == visited.end());
                visited.push_back(search.offset); search.accept(truth[radius+search.offset]); assert(++count <= 2*radius);
            }
            auto measured = search.window(), expected = cf::measure(truth, radius);
            assert(measured.early == expected.early && measured.late == expected.late && measured.lowerBound == expected.lowerBound);
        }
    }
    cf::AdaptiveSearch capped; capped.reset(60, 8); int calls = 0;
    while (!capped.done) { capped.accept(true); ++calls; }
    assert(capped.window().width() == 8 && capped.window().lowerBound && calls == 7);
    cf::AdaptiveSearch tight; tight.reset(60, 8); tight.accept(false); tight.accept(false);
    assert(tight.done && tight.window().width() == 1 && !tight.window().lowerBound);
    std::vector<cf::Input> inputs{{20, true, 1, true}, {21, false, 1, true}, {200, true, 1, true}, {400, true, 1, true}};
    auto targets = cf::checkpointTargets(inputs, 12, 2);
    assert(targets.size() == 2 && targets[0] == 6 && targets[1] == 386);
    assert(cf::checkpointTargets(inputs, 12, 0).empty());
    std::vector<cf::Input> candidate; assert(cf::candidateTrial(inputs, 0, 1, candidate));
    assert(cf::lastChangedTick(inputs, candidate) == 21);
    cf::Observation base, current; cf::RejoinEvidence evidence;
    evidence.quietWorld = true; evidence.rng = 42;
    assert(cf::canRejoin(base, current, evidence, evidence, 22, 21)); // Airborne is allowed.
    assert(!cf::canRejoin(base, current, evidence, evidence, 21, 21));
    auto changed = evidence; changed.rng = 43;
    assert(!cf::canRejoin(base, current, evidence, changed, 22, 21));
    changed = evidence; changed.causalHash = 2;
    assert(!cf::canRejoin(base, current, evidence, changed, 22, 21));
    changed = evidence; changed.quietWorld = false;
    assert(!cf::canRejoin(base, current, evidence, changed, 22, 21));
    current.values[1] = .000001;
    assert(cf::matches(base, current) && !cf::canRejoin(base, current, evidence, evidence, 22, 21));
    cf::AuditGate audits;
    for (int i = 1; i <= 10; ++i) assert(audits.consider() == (i == 10));
    audits.observed(false); assert(audits.disabled && audits.failures == 1 && !audits.consider());
    cf::ProbeCache cache; cache.put({1,-1,false}, {false,true});
    assert(cache.get({1,-1,false}, true) && !cache.get({1,1,false}) && !cache.get({1,-1,true}));
    cache.put({2,1,false}, {true,false}); assert(!cache.get({2,1,false},true));
    cache.clear(); assert(!cache.get({1,-1,false}));
    std::cout << "Scan support tests passed\n";
}
