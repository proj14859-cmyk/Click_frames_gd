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
    std::vector<cf::Input> input{{10,true,1,true},{10,true,1,false},{12,false,1,true}};
    std::vector<cf::Input> out;
    assert(!cf::trial(input,0,-11,out));
    assert(!cf::trial(input,0,3,out)); // cannot cross release
    assert(cf::trial(input,0,1,out) && !out[0].p1); // other player is independent
    assert(cf::trial(input,0,2,out) && out[1].down && !out[2].down); // stable same-tick order
    cf::Recording r; r.key = "test"; r.inputs = input; r.complete = true; r.verified = true;
    r.windows.assign(3, cf::Window{0,0,true,false}); r.trace.push_back({1,1.5f,2,3,4}); r.endTick = 20;
    assert(r.ready());
    r.windows[1].measured = false; assert(!r.ready()); r.windows[1].measured = true;
    std::stringstream stream; cf::write(stream,r); cf::Recording loaded;
    assert(cf::read(stream,"test",loaded) && loaded.ready() && loaded.inputs.size() == 3);
    assert(loaded.trace[0].x == 1.5f);
    std::stringstream wrong; cf::write(wrong,r); assert(!cf::read(wrong,"other",loaded));
    std::stringstream truncated("CLICKFRAMES 1"); assert(!cf::read(truncated,"test",loaded));
    assert(cf::fingerprint("level-a") != cf::fingerprint("level-b"));
    std::cout << "Timing tests passed\n";
}
