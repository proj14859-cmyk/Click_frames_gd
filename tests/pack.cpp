#include "../src/Pack.hpp"
#include <cassert>
#include <iostream>
#include <sstream>
int main() {
    assert(cf::slug("Bloodbath") == "bloodbath");
    assert(cf::slug("  Stereo Madness!! ") == "stereo-madness");
    assert(cf::slug("../../etc") == "etc");
    assert(cf::slug("???") == "level");
    assert(cf::safeName(cf::slug("Ünï cödé") + "-0"));
    assert(cf::runName("run-123.cfr") && !cf::runName("run-.cfr") && !cf::runName("run-1.cfr.bak"));
    assert(!cf::runName("../run-1.cfr") && !cf::runName("run-1/x.cfr") && !cf::runName("run-a.cfr"));

    cf::Pack p; p.folder = "bloodbath-10565740";
    p.files.push_back({"run-1.cfr", "CLICKFRAMES 2\nline two\r\n"});  // binary-safe: contains \r\n
    p.files.push_back({"run-22.cfr", ""});
    std::stringstream s; cf::packWrite(s, p);
    cf::Pack q; assert(cf::packRead(s, q));
    assert(q.folder == p.folder && q.files.size() == 2);
    assert(q.files[0].data == p.files[0].data && q.files[1].data.empty());

    auto bad = [](std::string text) { std::istringstream in(text); cf::Pack r; return cf::packRead(in, r); };
    assert(bad("CFPACK 1 a-1 1\nrun-1.cfr 3\nabc\n"));
    assert(!bad("CFPACK 2 a-1 1\nrun-1.cfr 3\nabc\n"));          // version
    assert(!bad("CFPACK 1 ../x 1\nrun-1.cfr 3\nabc\n"));         // folder escapes
    assert(!bad("CFPACK 1 a-1 1\n../run-1.cfr 3\nabc\n"));       // file escapes
    assert(!bad("CFPACK 1 a-1 1\nevil.exe 3\nabc\n"));           // not a run
    assert(!bad("CFPACK 1 a-1 1\nrun-1.cfr 99\nabc\n"));         // size lies
    assert(!bad("CFPACK 1 a-1 1\nrun-1.cfr 3\nabcX"));           // missing terminator
    assert(!bad("CFPACK 1 a-1 2\nrun-1.cfr 3\nabc\n"));          // count lies
    assert(!bad("CFPACK 1 a-1 0\n"));                            // empty
    assert(!bad("CFPACK 1 a-1 1\nrun-1.cfr 999999999\n"));       // huge size, tiny file
    assert(!bad("CFPACK 1 a-1 2\nrun-1.cfr 1\na\nrun-1.cfr 1\nb\n")); // duplicate target
    assert(!bad("CFPACK 1 a-1 1\nrun-1.cfr 1\na\ntrailing"));
    assert(!bad(""));
    std::cout << "Pack tests passed\n";
}
