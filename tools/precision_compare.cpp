#include "../src/Timing.hpp"
#include "../src/Precision.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
int main(int argc, char** argv) {
    if (argc != 2) { std::cerr << "Usage: precision_compare capture.cfr (read only; stdout CSV)\n"; return 1; }
    std::ifstream input(argv[1]); std::string magic, key; int version = 0;
    if (!(input >> magic >> version >> std::quoted(key))) { std::cerr << "Invalid CFR header\n"; return 1; }
    input.clear(); input.seekg(0);
    cf::Recording recording;
    if (!cf::read(input, key, recording)) { std::cerr << "Unsupported or invalid CFR\n"; return 1; }
    std::vector<cf::precision::Click> clicks;
    std::vector<size_t> events;
    std::vector<double> intervals;
    int previous = -1; double interval = -1;
    size_t eligible = 0;
    for (size_t i = 0; i < recording.inputs.size(); ++i) {
        auto const& e = recording.inputs[i];
        if (e.down && e.button == 1) { if (previous >= 0) interval = (e.tick-previous)*1000./240; previous = e.tick; }
        if (!cf::counted(e)) continue;
        ++eligible;
        if (i >= recording.windows.size() || !recording.windows[i].measured) continue;
        auto const& w = recording.windows[i];
        clicks.push_back({e.tick/240.,int64_t(clicks.size()+1),double(w.width()),1,w.lowerBound});
        events.push_back(i); intervals.push_back(interval);
    }
    std::cerr << "Measured inputs: " << clicks.size() << '/' << eligible << "; partial=" << (clicks.size()!=eligible)
        << "; verified=" << recording.verified << "; scope=" << recording.analysisPolicy << '\n';
    if (clicks.empty()) { std::cerr << "No measured windows; no L* values can be inferred from input spacing\n"; return 2; }
    cf::precision::Params params;
    auto result = cf::precision::calculate(clicks,params);
    if (!result.error.empty()) { std::cerr << result.error << '\n'; return 1; }
    std::cout << "event,tick,existing_press_interval_ms,window_ms,conservative";
    for (auto name : cf::precision::names) std::cout << ',' << name << "_Lstar," << name << "_sigma_ms";
    std::cout << '\n' << std::setprecision(12);
    for (size_t i = 0; i < result.prefixes.size(); ++i) {
        auto const& prefix = result.prefixes[i];
        std::cout << events[i] << ',' << recording.inputs[events[i]].tick << ',';
        if (intervals[i] >= 0) std::cout << intervals[i];
        std::cout << ',' << clicks[i].windowFrames*1000./240 << ',' << prefix.conservative;
        for (auto const& metric : prefix.metrics) {
            std::cout << ','; if (metric.valid) std::cout << metric.lstar;
            std::cout << ','; if (metric.valid) std::cout << metric.sigmaMs();
        }
        std::cout << '\n';
    }
}
