#include "../src/CollectorData.hpp"
#include <cassert>
#include <iostream>
int main() {
    assert(cfc::jsonEscape("a\"b\nc") == "a\\\"b\\nc");
    assert(cfc::safeFolder("A/B: C") == "A-B-C");
    cfc::Attempt a; a.id="x";a.levelName="Test";a.result="death";
    cfc::InputEvent input;input.tick=10;input.down=true;input.button=1;input.p1=true;input.x=2;input.y=3;input.mode=0;a.inputs.push_back(input);
    a.frames.push_back({10,25,2,3,4,1,0,0,0,1});
    auto meta=cfc::metadata(a),inputs=cfc::inputsTsv(a),trajectory=cfc::trajectoryTsv(a);
    assert(meta.find("\"schema\": \"CF-COLLECTOR-1\"")!=std::string::npos);
    assert(meta.find("\"input_events\": 1")!=std::string::npos);
    assert(inputs.find("10\t1\t1\t1\t2\t3\t0")!=std::string::npos);
    assert(trajectory.find("tick\tpercent") == 0);
    assert(cfc::fnv1a("level") == cfc::fnv1a("level") && cfc::fnv1a("level") != cfc::fnv1a("other"));
    std::cout << "Collector serialization tests passed\n";
}
