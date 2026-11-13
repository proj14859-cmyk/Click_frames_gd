#include "../src/Analysis.hpp"
#include <cassert>
#include <sstream>
#include <iostream>
int main() {
    cf::Observation original, repeated;
    assert(cf::matches(original, repeated));
    repeated.values[2] = .000001; assert(!cf::matches(original, repeated));
    repeated = original; repeated.flags[7] = 1; assert(!cf::matches(original, repeated));
    repeated = original; repeated.values[0] = std::numeric_limits<double>::quiet_NaN();
    assert(!cf::matches(repeated, repeated));
    repeated = original; repeated.values[10] = 90; repeated.values[4] = 999;
    repeated.flags[1] = 4; assert(cf::matches(original, repeated)); // Visual rotation and inactive P2.
    original.flags[6] = repeated.flags[6] = 1; assert(!cf::matches(original, repeated));
    original = {}; repeated = {}; repeated.values[0] = .00001; assert(cf::matches(original, repeated));
    repeated.values[0] = .01; assert(!cf::matches(original, repeated));
    cf::CausalEvent cause{10, 1, 22, 1, 0}, later{11, 1, 22, 1, 0};
    assert(cf::sameCause(cause, later, false) && !cf::sameCause(cause, later, true));
    later.object = 23; assert(!cf::sameCause(cause, later, false));
    assert(!cf::countsAsCause(6)); // Raw feedback is not an event-trigger dispatch.
    assert(cf::countsAsCause(8) && cf::countsAsCause(1) && cf::countsAsCause(5));
    assert(!cf::countsAsCause(0) && !cf::countsAsCause(9));
    assert(cf::orbWasActivated(false, true));
    assert(!cf::orbWasActivated(false, false) && !cf::orbWasActivated(true, true) && !cf::orbWasActivated(true, false));
    auto detail = cf::causalDifference(&cause, later, 1);
    assert(detail.find("Expected: tick=10") != std::string::npos);
    assert(detail.find("object=22") != std::string::npos && detail.find("object=23") != std::string::npos);
    assert(detail.find("Observed: tick=11") != std::string::npos);
    assert(cf::causalDifference(nullptr, later, 2).find("<end of reference>") != std::string::npos);
    auto activated = cause; activated.kind = 8;
    assert(!cf::sameCause(cause, activated, false));
    activated = cause; activated.player = 2; assert(!cf::sameCause(cause, activated, false));
    activated = cause; activated.detail = 3; assert(!cf::sameCause(cause, activated, false));
    std::vector<cf::Input> tap{{10, true, 1, true, 0, 0, 0}, {11, false, 1, true, 0, 0, 0}, {20, true, 1, true, 0, 0, 0}}, moved;
    assert(cf::candidateTrial(tap, 0, 3, moved));
    assert(moved[0].tick == 13 && moved[1].tick == 14);
    assert(!cf::candidateTrial(tap, 0, 10, moved));
    tap[0].mode = tap[1].mode = 5;
    assert(!cf::candidateTrial(tap, 0, 3, moved)); // Robot release remains independently meaningful.
    std::vector<cf::Input> flight{{10, true, 1, true, 0, 0, 1}, {20, false, 1, true, 0, 0, 1}, {30, true, 1, true, 0, 0, 1}};
    assert(cf::recoveryTrial(flight, 0, 1, 1, -1, moved));
    assert(moved[0].tick == 11 && moved[1].tick == 19 && moved[2].tick == 30);
    for (size_t i = 0; i < moved.size(); ++i) assert(moved[i].sourceIndex == i);
    assert(!cf::recoveryTrial(flight, 0, 1, 1, -15, moved));
    assert(!cf::recoveryTrial(flight, 0, 1, 0, 1, moved));
    std::vector<cf::Input> endInputs{{10, true, 1, true}, {15, false, 1, true}};
    assert(!cf::allPressesDispatched(endInputs, 0));
    assert(cf::allPressesDispatched(endInputs, 1));
    assert(cf::allPressesDispatched(endInputs, 2));
    assert(!cf::allPressesDispatched(endInputs, 3));
    assert(cf::allPressesDispatched({}, 0));
    endInputs.push_back({20, true, 1, false});
    assert(!cf::allPressesDispatched(endInputs, 1));
    for (int radius = 1; radius <= 6; ++radius) {
        for (unsigned mask = 0; mask < (1u << (2 * radius)); ++mask) {
            std::vector<bool> truth(2 * radius + 1, false); truth[radius] = true;
            int bit = 0;
            for (int i = 0; i <= 2 * radius; ++i) if (i != radius) truth[i] = (mask >> bit++) & 1u;
            cf::ContinuousSearch search; search.reset(radius); int calls = 0;
            while (!search.done) {
                assert(search.offset && std::abs(search.offset) <= radius);
                search.accept(truth[search.offset + radius]); assert(++calls <= 2 * radius);
            }
            auto fast = search.window(), exhaustive = cf::measure(truth, radius);
            assert(fast.early == exhaustive.early && fast.late == exhaustive.late && fast.lowerBound == exhaustive.lowerBound);
        }
    }
    cf::ContinuousSearch narrow; narrow.reset(12); narrow.accept(false); narrow.accept(false);
    assert(narrow.done && narrow.window().width() == 1);
    cf::Landing reference{true, 200, 1234, 100, 30}, candidate{true, 203, 1234, 120, 32};
    assert(cf::sameLanding(reference, candidate, 60));
    candidate.platform = 4567; assert(!cf::sameLanding(reference, candidate, 60));
    candidate.platform = 1234; candidate.x = 200; assert(!cf::sameLanding(reference, candidate, 60));
    candidate.x = 100; candidate.y = 100; assert(!cf::sameLanding(reference, candidate, 60));
    cf::Predictions predictions{{2, {{-1, 120, false, {{110, 1, 2, 3, 4}, {119, 5, 6, 7, 8}}}}}}, loaded;
    std::stringstream out; cf::writePredictions(out, "hash", predictions);
    assert(cf::readPredictions(out, "hash", 3, loaded) && loaded[2][0].points.size() == 2);
    std::stringstream mismatch; cf::writePredictions(mismatch, "hash", predictions);
    assert(!cf::readPredictions(mismatch, "other", 3, loaded));
    std::stringstream bad("CFPRED 1 hash 1\n3 -1 0 10 9 1 2 3 4\n");
    assert(!cf::readPredictions(bad, "hash", 3, loaded));
    std::vector<cf::Input> inputs{{10, true, 1, true}, {11, true, 1, false}}, trial;
    assert(cf::trial(inputs, 0, 2, trial) && trial[0].sourceIndex == 1 && trial[1].sourceIndex == 0);
    cf::Recording recording; recording.key = "profile"; recording.backend = "cos240"; recording.analysisPolicy = 1;
    std::stringstream capture; cf::write(capture, recording); cf::Recording restored;
    assert(cf::read(capture, "profile", restored) && restored.backend == "cos240" && restored.analysisPolicy == 1);
    cf::Recording local; local.endTick = 40;
    local.inputs = {{10, true, 1, true}, {12, true, 1, false}, {15, false, 1, true}, {20, true, 1, true}, {30, false, 1, false, 0, 0, 1}};
    std::map<int, cf::PhysicsReference> frames;
    frames[20] = {{20, 100, 30, 200, 40}, 0, 1, false, true};
    frames[30] = {{30, 110, 35, 210, 45}, 0, 1, false, true};
    frames[40] = {{40, 120, 40, 220, 50}, 0, 1, false, true};
    auto exits = cf::segmentExits(local, frames);
    assert(exits[0].tick == 20 && exits[1].tick == 30 && exits[3].tick == 40 && exits[4].tick == 40);
    assert(cf::atSegmentExit(exits[1], 210, 45, 1, true));
    assert(!cf::atSegmentExit(exits[1], 210, 45, 0, true));
    assert(!cf::atSegmentExit(exits[1], 210, 45, 1, false));
    assert(!cf::atSegmentExit(exits[1], 210, 90, 1, true));
    frames.erase(20); exits = cf::segmentExits(local, frames); assert(!exits[0].known);
    struct State { int tick; bool rejected; };
    std::vector<State> states{{10, false}, {30, false}, {20, false}};
    assert(cf::snapshotBefore(states, 31) == 1 && cf::snapshotBefore(states, 30) == 2);
    states[1].rejected = true; assert(cf::snapshotBefore(states, 50) == 2);
    assert(cf::snapshotBefore(states, 5) == -1);
    std::cout << "Analysis tests passed\n";
}
