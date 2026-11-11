#pragma once
#include "Timing.hpp"
#include <bit>
#include <sstream>

namespace cf {
inline std::vector<size_t> closedWindows(Recording const& r) {
    std::vector<size_t> result;
    if (!r.ready()) return result;
    for (size_t i=0; i<r.inputs.size(); ++i)
        if (counted(r.inputs[i]) && r.windows[i].measured && !r.windows[i].lowerBound) result.push_back(i);
    return result;
}
inline std::string fwcBytes(Recording const& r) {
    auto indices = closedWindows(r); std::string bytes = "FWC2";
    auto integer = [&bytes](uint64_t v, int n) { for (int i=0;i<n;++i) { bytes.push_back(char(v & 255)); v >>= 8; } };
    integer(std::bit_cast<uint64_t>(240.),8); integer(indices.size(),4);
    for (auto i : indices) {
        integer(uint32_t(r.inputs[i].tick),4); integer(std::bit_cast<uint64_t>(double(r.windows[i].width())),8);
        integer(1 | (r.inputs[i].p1 ? 0 : 2),1); integer(1,4);
    }
    return bytes;
}
inline std::string nandlJson(Recording const& r) {
    std::ostringstream out;
    out << "{\"format\":\"nandl-calculator\",\"version\":1,\"settings\":{\"gameFps\":240,\"windowFps\":240,\"respawnSeconds\":0,\"timeUnit\":\"frames\"},\"frameWindows\":[";
    size_t number=0;
    for (auto i : closedWindows(r)) {
        if (number) out << ',';
        out << "{\"input\":" << ++number << ",\"timePosition\":" << r.inputs[i].tick << ",\"frameWindow\":" << r.windows[i].width()
            << ",\"isPlayer2\":" << (r.inputs[i].p1 ? "false" : "true") << '}';
    }
    return out.str() + "]}\n";
}
}
