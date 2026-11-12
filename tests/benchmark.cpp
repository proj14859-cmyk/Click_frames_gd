#include "../src/Analysis.hpp"
#include "../src/Precision.hpp"
#include <cassert>
#include <chrono>
#include <iostream>
int main() {
    std::cout << "Synthetic algorithm benchmark; no GD physics, level or device timing\n";
    for(bool approximate:{false,true}){
        cf::GroupedSearch grouped;grouped.approximate=approximate;grouped.reset(20);int probes=0;
        while(!grouped.done){grouped.accept(true);++probes;}
        assert(probes==(approximate?8:40));
        std::cout<<"v106_grouped_open_range,"<<(approximate?"estimated":"exact")<<",probes,"<<probes<<",width,"<<grouped.window().width()<<"+\n";
    }
    std::cout<<"Historical v104 adaptive-cap benchmark below (not v106 engine)\n";
    std::cout << "events,previous_probes,adaptive_probes,probes_saved_percent,exact_windows,lower_bounds\n";
    for (int events : {20,154}) {
        int previous = 0, adaptive = 0, exact = 0, bounds = 0;
        for (int i = 0; i < events; ++i) {
            int halfWidth = i%4 == 3 ? 100 : i%4;
            cf::ContinuousSearch old; old.reset(12);
            while (!old.done) { old.accept(std::abs(old.offset) <= halfWidth); ++previous; }
            cf::AdaptiveSearch current; current.reset(12,8);
            while (!current.done) { current.accept(std::abs(current.offset) <= halfWidth); ++adaptive; }
            if (current.window().lowerBound) { ++bounds; assert(current.window().width() >= 8); }
            else { ++exact; assert(current.window().width() == old.window().width()); }
        }
        std::cout << events << ',' << previous << ',' << adaptive << ',' << 100.*(previous-adaptive)/previous << ',' << exact << ',' << bounds << '\n';
        assert(adaptive < previous); // Broad results carry less information, explicitly marked N+.
    }
    using namespace cf::precision;
    std::vector<Click> clicks;
    for (int i = 1; i <= 1000; ++i) clicks.push_back({i*.2,i,double(1+i%9),1,false});
    Params p;
    (void)erfcFast(.5); // separate one-time LUT initialization from repeated solves
    auto start = std::chrono::steady_clock::now();
    auto root = solveL(clicks, Weighting::Base,p);
    auto ms = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    assert(root.valid);
    std::cout << "1000_click_final_Lstar_ms," << ms << ",target_ms,50\n";
    start = std::chrono::steady_clock::now();
    auto cumulative = calculate(clicks,p);
    ms = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    assert(cumulative.error.empty() && cumulative.prefixes.size() == 1000);
    std::cout << "1000_click_all_prefixes_all_8_models_ms," << ms << '\n';
}
