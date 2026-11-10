#include "../src/Timing.hpp"
#include <cassert>
#include <sstream>
#include <iostream>
int main() {
    auto w = cf::measure({false, false, true, false, false}, 2);
    assert(w.measured && w.width() == 1 && !w.lowerBound);
    w = cf::measure({true, false, true, true, false}, 2);
    assert(w.width() == 2 && !w.lowerBound); // ignore disconnected successful islands
    w = cf::measure({true, true, true, true, true}, 2);
    assert(w.width() == 5 && w.lowerBound);
    assert(!cf::measure({true, false, true}, 1).measured);
    std::vector<cf::Input> input{{10,true,1,true,100.f,200.f},{10,true,1,false,110.f,210.f},{12,false,1,true,120.f,220.f}};
    std::vector<cf::Input> out;
    assert(!cf::trial(input,0,-11,out));
    assert(!cf::trial(input,0,3,out)); // cannot cross release
    assert(cf::trial(input,0,1,out) && !out[0].p1); // other player is independent
    assert(cf::trial(input,0,2,out) && out[1].down && !out[2].down); // stable same-tick order
    cf::Recording r; r.key = "test"; r.inputs = input; r.complete = true; r.verified = true;
    r.windows.assign(3, cf::Window{0,0,true,false}); r.trace.push_back({1,1.5f,2,3,4}); r.endTick = 20;
    assert(r.ready());
    auto rows = cf::makeRows(r, 8);
    assert(rows.size() == 1 && rows[0].from == 1 && !rows[0].open && cf::label(rows[0]) == "1");
    r.windows[1] = {1, 1, true, false};

    r.windows[1] = {0, 0, true, false};
    r.windows[1].measured = false; assert(!r.ready()); r.windows[1].measured = true;
    std::stringstream stream; cf::write(stream,r); cf::Recording loaded;
    assert(cf::read(stream,"test",loaded) && loaded.ready() && loaded.inputs.size() == 3);
    assert(loaded.inputs[0].x == 100.f && loaded.inputs[2].y == 220.f);
    assert(loaded.trace[0].x == 1.5f);
    r.inputs[2].mode = 5;
    r.windows[2].measured = false;
    assert(!r.ready()); // robot release must be measured
    r.inputs[2].mode = 0;
    assert(r.ready()); // cube release is still recorded, but not analyzed/counted
    std::stringstream modes; r.inputs[2].mode = 4; cf::write(modes, r);
    assert(cf::read(modes, "test", loaded) && loaded.inputs[2].mode == 4);
    std::stringstream legacy("CLICKFRAMES 2 \"old\" 1 1 20 12 1 0\n10 1 1 1 0 0 0 0 1 0\n");
    assert(cf::read(legacy, "old", loaded) && loaded.ready() && loaded.inputs[0].mode == -1);
    for (int mode = -1; mode <= 7; ++mode) {
        cf::Input release{10, false, 1, true, 0, 0, mode};
        assert(cf::counted(release) == (mode == 1 || mode == 4 || mode == 5 || mode == 7));
    }
    std::stringstream wrong; cf::write(wrong,r); assert(!cf::read(wrong,"other",loaded));
    std::stringstream old("CLICKFRAMES 1"); assert(!cf::read(old,"test",loaded));
    std::stringstream truncated("CLICKFRAMES 2"); assert(!cf::read(truncated,"test",loaded));
    assert(cf::fingerprint("level-a") != cf::fingerprint("level-b"));
    std::vector<cf::Input> run{{5,true,1,true,0.f,0.f},{9,false,1,true,0.f,0.f}}, moved;
    assert(cf::shiftAll(run, -5, moved) && moved[0].tick == 0 && moved[1].tick == 4);
    assert(!cf::shiftAll(run, -6, moved) && moved[0].tick == 0); // failed shift leaves out untouched
    assert(cf::shiftAll(run, 2, run) && run[0].tick == 7 && run[1].tick == 11); // in place
    cf::Recording wide; wide.radius = 20; // 6 distinct widths, 4 rows: 2, 3, 4 exact, then everything wider shares "5+"
    for (int w : {2, 3, 4, 5, 6, 7}) { wide.inputs.push_back({1, true, 1, true, 0.f, 0.f}); wide.windows.push_back({w - 1, 0, true, false}); }
    auto wr = cf::makeRows(wide, 4);
    assert(wr.size() == 4 && wr[3].from == 5 && wr[3].to == 7 && wr[3].open);
    for (size_t i = 0; i < wide.windows.size(); ++i) assert(cf::rowOf(wr, wide.windows[i]) == std::min<int>(i, 3));
    assert(cf::makeRows(wide, 1).size() == 2 && cf::makeRows(cf::Recording{}, 8).empty());
    auto red = cf::colorFor(1), green = cf::colorFor(50);
    assert(red.r == 255 && red.g == 75 && green.g == 245 && green.r == 105);
    run[0].tick = std::numeric_limits<int>::max();
    auto previous = moved;
    assert(!cf::shiftAll(run, 2, moved) && moved[0].tick == previous[0].tick);
    // Include empty widths between the observed endpoints; lower-bound only on top.
    cf::Recording gaps; gaps.inputs.resize(2); for (auto& e : gaps.inputs) e.down = true;
    gaps.windows = {{2, 0, true, true}, {4, 0, true, false}};
    auto gr = cf::makeRows(gaps, 8);
    assert(gr.size() == 3 && gr[0].from == 3 && gr[1].from == 4 && gr[2].from == 5);
    assert(!gr[0].open && !gr[2].open);
    gaps.windows[1].lowerBound = true;
    gr = cf::makeRows(gaps, 8); assert(gr.back().open);
    std::cout << "Timing tests passed\n";
}
