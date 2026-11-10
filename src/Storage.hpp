#pragma once
#include "Timing.hpp"
#include <sstream>

namespace cf {
inline std::string inputsCsv(Recording const& r) {
    std::ostringstream out;
    out << "tick,down,button,player,x,y,mode\n" << std::setprecision(9);
    for (auto const& e : r.inputs)
        out << e.tick << ',' << e.down << ',' << e.button << ',' << (e.p1 ? 1 : 2) << ',' << e.x << ',' << e.y << ',' << e.mode << '\n';
    return out.str();
}
inline std::string traceCsv(Recording const& r) {
    std::ostringstream out;
    out << "tick,x1,y1,x2,y2\n" << std::setprecision(9);
    for (auto const& e : r.trace)
        out << e.tick << ',' << e.x << ',' << e.y << ',' << e.x2 << ',' << e.y2 << '\n';
    return out.str();
}
inline std::string windowsCsv(Recording const& r) {
    std::ostringstream out;
    out << "event,tick,measured,early,late,width,lower_bound,width_ms\n" << std::setprecision(9);
    for (size_t i = 0; i < r.inputs.size(); ++i) {
        auto w = i < r.windows.size() ? r.windows[i] : Window{};
        out << i << ',' << r.inputs[i].tick << ',' << w.measured << ',' << w.early << ',' << w.late << ',';
        if (w.measured) out << w.width();
        out << ',' << w.lowerBound << ',';
        if (w.measured) out << w.width() * 1000. / 240.;
        out << '\n';
    }
    return out.str();
}
inline std::string scanReport(Recording const& r, std::string const& status) {
    size_t measured = 0, total = 0;
    for (size_t i = 0; i < r.inputs.size(); ++i) {
        if (!counted(r.inputs[i])) continue;
        ++total;
        if (i < r.windows.size() && r.windows[i].measured) ++measured;
    }
    std::ostringstream out;
    out << "Click Frames GD\nFingerprint: " << r.key << "\nTPS: 240\nVerified: " << r.verified
        << "\nBackend: " << r.backend << "\nLanding policy: " << r.analysisPolicy
        << "\nComplete: " << r.complete << "\nMeasured jump events: " << measured << '/' << total
        << "\nSearch radius: " << r.radius << "\nStatus: " << status
        << "\nScope: " << (r.analysisPolicy == 2 ? "local segment (not full-route certification)" : "full recorded route")
        << "\nAuthoritative files: capture.flr + matching playback.flp. CSVs are derived; edits are visual only.\n";
    return out.str();
}
inline std::string metadataJson(Recording const& r) {
    // fingerprint is a decimal hash. Encode defensively for general callers too.
    std::ostringstream key;
    for (unsigned char c : r.key) {
        if (c == '"' || c == '\\') key << '\\' << c;
        else if (c < 32) key << "\\u" << std::hex << std::setw(4) << std::setfill('0') << int(c) << std::dec;
        else key << c;
    }
    std::ostringstream out;
    out << "{\n  \"schema\": 1,\n  \"fingerprint\": \"" << key.str()
        << "\",\n  \"gd\": \"2.2081\",\n  \"geode\": \"5.11.2\",\n  \"tps\": 240,\n"
        << "  \"storage_schema\": 2,\n  \"capture_format\": \"CFCAP 1\",\n  \"playback_format\": \"CFPLAY 1\",\n  \"backend\": " << std::quoted(r.backend)
        << ",\n  \"analysis_policy\": " << r.analysisPolicy
        << ",\n  \"capture\": \"capture.flr\",\n  \"playback\": \"playback.flp\",\n  \"complete\": " << (r.complete ? "true" : "false")
        << ",\n  \"verified\": " << (r.verified ? "true" : "false") << ",\n  \"end_tick\": " << r.endTick << "\n}\n";
    return out.str();
}
}
