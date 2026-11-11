#include "../src/Precision.hpp"
#include "../src/PrecisionJob.hpp"
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
using namespace cf::precision;
int main() {
    double error = 0;
    for (int i = 0; i <= 200000; ++i) {
        double x = i * 6. / 200000.; error = std::max(error, std::abs(erfcFast(x) - std::erfc(x)));
    }
    assert(error < 1e-9);
    assert(erfcFast(7) == 0 && erfcFast(-1) == 1);
    Params p;
    std::vector<Click> one{{1, 1, 1, 1, false}};
    double known = 200;
    p.target = 1. / std::erf(one[0].windowFrames / p.fps * .5 * std::sqrt(.5) * known);
    auto root = solveL(one, Weighting::Base, p);
    assert(root.valid && std::abs(root.lstar-known) < 1e-4);
    assert(std::abs(root.sigmaMs() - 5) < 1e-5);
    auto distantGuess = solveL(one, Weighting::Base, p, maxL/2);
    assert(distantGuess.valid && std::abs(distantGuess.lstar-known) < 1e-3);
    auto wider = one; wider[0].windowFrames = 5;
    auto loose = solveL(wider, Weighting::Base, p);
    assert(loose.valid && std::abs(loose.lstar * 5 - root.lstar) < 1e-3);
    p.target = 86400;
    std::vector<Click> clicks{{1, 1, 1, 1, false}, {2, 2, 3, 1, true}, {3, 3, 8, 1, false}};
    auto result = calculate(clicks, p);
    assert(result.error.empty() && result.prefixes.size() == 3);
    assert(!result.prefixes[0].conservative && result.prefixes[1].conservative && result.prefixes[2].conservative);
    for (auto const& prefix : result.prefixes) for (auto const& metric : prefix.metrics)
        assert(metric.valid && std::isfinite(metric.lstar) && metric.sigmaMs() > 0);
    assert(result.prefixes.back().metrics[1].lstar >= result.prefixes.back().metrics[0].lstar);
    assert(result.prefixes.back().metrics[2].lstar >= result.prefixes.back().metrics[0].lstar);
    // Independent std::erf + long-double oracle for all eight upstream weights.
    constexpr bool nerve[] = {false,true,false,false,true,true,false,true};
    constexpr bool fatigue[] = {false,false,true,false,true,false,true,true};
    constexpr bool cps[] = {false,false,false,true,false,true,true,true};
    for (size_t k = 0; k < 8; ++k) {
        auto expected = [&](long double L) {
            long double survival = 1, fails = 0, previous = 0;
            for (auto const& click : clicks) {
                long double t = p.respawn + click.time;
                long double w = click.windowFrames / p.fps / (2*std::sqrt(2.L));
                if (nerve[k]) w *= std::exp(-p.kT*t);
                if (fatigue[k]) w *= std::exp(-p.kU*click.index);
                if (cps[k]) w *= std::pow(4.L/std::max(1.L,2.L/(t-previous)),p.kC);
                long double probability = std::erf(w*L);
                fails += t*survival*(1-probability); survival *= probability; previous = t;
            }
            return (p.respawn+clicks.back().time) + fails/survival;
        };
        long double left = .001L, right = 1000000;
        for (int iteration = 0; iteration < 100; ++iteration) {
            auto mid = (left+right)/2;
            if (expected(mid) > p.target) left = mid; else right = mid;
        }
        auto metric = solveL(clicks,Weighting(k),p);
        assert(metric.valid && std::abs(metric.lstar-double((left+right)/2)) < 1e-5*metric.lstar);
    }
    auto prepared = prepare(one, p);
    assert(expectedClear(prepared.times, prepared.weights[0], 100) > expectedClear(prepared.times, prepared.weights[0], 200));
    assert(expectedClear({}, {}, 1) == unresolved);
    auto micro = one; micro[0].subInputs = 3;
    auto flat = prepare(micro, p); assert(flat.times.size() == 3 && flat.ends[0] == 3);
    assert(std::abs(flat.times[1] - flat.times[0] - 1./720) < 1e-12);
    auto bad = p; bad.fps = 0; assert(!solveL(one, Weighting::Base, bad).valid);
    bad = p; bad.target = .5; assert(!solveL(one, Weighting::Base, bad).valid);
    bad = p; bad.kT = -1; assert(!prepare(one, bad).error.empty());
    auto invalid = one; invalid[0].windowFrames = std::numeric_limits<double>::quiet_NaN();
    assert(!prepare(invalid, p).error.empty());
    auto backwards = clicks; backwards[1].time = .1; assert(!prepare(backwards, p).error.empty());
    assert(calculate(clicks, p, [] { return true; }).cancelled);
    auto laterIndex = one; laterIndex[0].index = 100;
    auto tired = solveL(laterIndex, Weighting::F, p), fresh = solveL(one, Weighting::F, p);
    assert(tired.valid && fresh.valid && tired.lstar > fresh.lstar);
    auto zero = one; zero[0].time = 0; assert(!solveL(zero, Weighting::Base, p).valid);
    auto duplicatedIndex = clicks; duplicatedIndex[1].index = 1; assert(!prepare(duplicatedIndex,p).error.empty());
    Job job;
    job.request("obsolete", clicks, p); job.poll();
    job.request("current", one, p);
    for (int i = 0; i < 5000 && !job.result(); ++i) { job.poll(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    assert(job.result() && job.key() == "current" && job.result()->prefixes.size() == 1);
    job.cancel(); job.poll(); assert(!job.result() && !job.busy());
    std::vector<Click> large;
    for (int i = 1; i <= 1000; ++i) large.push_back({i * .2, i, double(1+i%9), 1, false});
    auto start = std::chrono::steady_clock::now();
    auto largeRoot = solveL(large, Weighting::Base, p);
    auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-start).count();
    assert(largeRoot.valid && std::isfinite(largeRoot.lstar));
    // Runtime is reported, never turned into a flaky hardware-dependent assertion.
    std::cout << "Precision tests passed; LUT max error=" << error << "; 1000-click final solve=" << elapsed << " ms\n";
}
