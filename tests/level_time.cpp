#include "../src/LevelTime.hpp"
#include <cassert>
#include <cmath>

int main() {
    using namespace cf::leveltime;
    Total fiveHundred{120000,true};
    assert(progressClock(fiveHundred,20.,350,false,true,true)=="100.00 / 500.00 s");
    assert(progressClock(fiveHundred,50.,1,true,true,false)=="250 / 500 s");
    assert(progressClock(fiveHundred,100.,100,false,true,true)=="500.00 / 500.00 s");
    assert(progressClock(std::nullopt,75.,5,true,true,true)=="-- / -- s");
    // Saved completion wins and is exact, whatever the live progress or portal estimate says.
    auto t = total(9216, 1830, 20., false, 9000);
    assert(t && t->exact && t->ticks == 9216);
    assert(format(1830, t) == "7.62 s / 38.40 s");

    // Extrapolation fallback: 20% after 7.62 s -> ~38.1 s, shown with "~".
    t = total(0, 1830, 20., false);
    assert(t && !t->exact && t->ticks == 9150);
    assert(format(1830, t) == "7.62 s / ~38.12 s");

    // Portal estimate wins over progress extrapolation when saved is absent, even before 5%.
    t = total(0, 100, 1.0, false, 4800);
    assert(t && !t->exact && t->ticks == 4800);
    assert(format(100, t) == "0.42 s / ~20.00 s");

    // Too early without portal estimate, start position / test mode / platformer, or no progress: no invented total.
    assert(!total(0, 100, 4.9, false));
    assert(!total(0, 1830, 20., true, 4800));
    assert(!total(0, 0, 50., false));
    assert(!total(0, 100, std::nan(""), false));
    assert(format(1830, std::nullopt) == "7.62 s / --");

    // Corrupt saved values are ignored; the estimate never falls below elapsed time.
    assert(!validTicks(0) && !validTicks(-5) && !validTicks(maxTicks + 1));
    t = total(-1, 1000, 100., false);
    assert(t && !t->exact && t->ticks == 1000);

    // Portal estimation tests:
    std::array<double, speedCount> rates = {1.0, 1.5, 2.0, 2.5, 3.0};

    // 1. Constant speed (no portals): 3600 units at speed 1 (1.5 u/tick) -> 2400 ticks.
    auto est = estimateTicks(0., 3600., 1, {}, rates);
    assert(est && *est == 2400);

    // 2. Single portal: 0..1500 at speed 1 (1.5 -> 1000 ticks), 1500..3500 at speed 2 (2.0 -> 1000 ticks) -> 2000 ticks.
    est = estimateTicks(0., 3500., 1, {{1500., 2}}, rates);
    assert(est && *est == 2000);

    // 3. Multiple portals (unsorted input):
    // 0..1000 at speed 0 (1.0 -> 1000), 1000..2500 at speed 1 (1.5 -> 1000), 2500..5500 at speed 4 (3.0 -> 1000) -> 3000 ticks.
    est = estimateTicks(0., 5500., 0, {{2500., 4}, {1000., 1}}, rates);
    assert(est && *est == 3000);

    // 4. Portals behind startX or at/beyond endX are ignored.
    est = estimateTicks(500., 2000., 1, {{-100., 4}, {500., 3}, {2000., 0}, {2500., 2}}, rates);
    assert(est && *est == 1000);

    // 5. Unlearned speed (0.0) on active segment returns nullopt; unlearned unused speed is allowed.
    std::array<double, speedCount> partialRates = {0., 1.5, 2.0, 0., 0.};
    assert(estimateTicks(0., 3500., 1, {{1500., 2}}, partialRates).has_value());
    assert(!estimateTicks(0., 3500., 0, {{1500., 2}}, partialRates).has_value());
    assert(!estimateTicks(0., 3500., 1, {{1500., 3}}, partialRates).has_value());

    // 6. Corrupt values return nullopt.
    assert(!estimateTicks(1000., 500., 1, {}, rates).has_value());
    assert(!estimateTicks(std::nan(""), 1000., 1, {}, rates).has_value());
    assert(!estimateTicks(0., 1000., -1, {}, rates).has_value());
    assert(!estimateTicks(0., 1000., 5, {}, rates).has_value());
    assert(!estimateTicks(0., 1000., 1, {{500., -1}}, rates).has_value());
    assert(!estimateTicks(0., 1000., 1, {{std::nan(""), 2}}, rates).has_value());

    // 7. SpeedLearner learns median after 60 steady ticks with < 1% spread and rejects jumps.
    SpeedLearner learner;
    double x = 0.;
    std::optional<std::pair<int, double>> learned;
    for (int tick = 0; tick <= 60; ++tick) {
        learned = learner.observe(tick, x, 0.9, true);
        x += 1.298;
    }
    assert(learned && learned->first == 1 && std::abs(learned->second - 1.298) < 1e-6);

    // Portal ID and factor helpers.
    assert(speedIndexFromPortalId(200) == 0 && speedIndexFromPortalId(201) == 1 &&
           speedIndexFromPortalId(202) == 2 && speedIndexFromPortalId(203) == 3 &&
           speedIndexFromPortalId(1334) == 4 && speedIndexFromPortalId(999) == -1);
    assert(speedIndexFromFactor(0.7) == 0 && speedIndexFromFactor(0.9) == 1 &&
           speedIndexFromFactor(1.1) == 2 && speedIndexFromFactor(1.3) == 3 &&
           speedIndexFromFactor(1.6) == 4 && speedIndexFromFactor(0.0) == -1);
    return 0;
}
