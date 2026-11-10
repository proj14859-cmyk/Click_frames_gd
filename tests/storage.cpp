#include "../src/Storage.hpp"
#include <cassert>
#include <iostream>
int main() {
    cf::Recording r; r.key = "1539950219185091717";
    r.inputs = {{104, true, 1, true, 76.f, 328.f}, {182, false, 1, true, 138.f, 351.f}};
    r.trace = {{0, 0, 105, 0, 0}};
    r.windows = {{0, 0, false, false}, {2, 1, true, true}};
    assert(cf::inputsCsv(r).find("104,1,1,1,76,328,-1\n") != std::string::npos);
    assert(cf::traceCsv(r).find("0,0,105,0,0\n") != std::string::npos);
    assert(cf::windowsCsv(r).find("0,104,0,0,0,,0,\n") != std::string::npos);
    assert(cf::windowsCsv(r).find("1,182,1,2,1,4,1,") != std::string::npos);
    assert(cf::scanReport(r, "Stopped").find("Measured jump events: 0/1") != std::string::npos);
    assert(cf::metadataJson(r).find("\"verified\": false") != std::string::npos);
    r.key = "quote\"\n";
    assert(cf::metadataJson(r).find("quote\\\"\\u000a") != std::string::npos);
    std::cout << "Storage tests passed\n";
}
