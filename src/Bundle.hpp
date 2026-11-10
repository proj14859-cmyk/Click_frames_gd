#pragma once
#include "Storage.hpp"
#include <filesystem>
#include <fstream>
#include <functional>

namespace cf {
namespace fs = std::filesystem;
constexpr size_t MAX_BUNDLE_BYTES = 64u * 1024 * 1024;
inline std::string readFile(fs::path const& file) {
    std::error_code ec; auto size = fs::file_size(file, ec);
    if (ec || size > MAX_BUNDLE_BYTES) return {};
    std::ifstream in(file, std::ios::binary);
    std::string bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    return in.bad() ? std::string{} : bytes;
}
inline bool replaceFile(fs::path const& file, std::string const& bytes) {
    if (bytes.size() > MAX_BUNDLE_BYTES) return false;
    std::error_code ec; fs::create_directories(file.parent_path(), ec); if (ec) return false;
    auto tmp = file; tmp += ".tmp";
    std::ofstream out(tmp, std::ios::binary); out << bytes; out.close(); if (!out) return false;
    bool existed = fs::exists(file, ec); if (ec) return false;
    auto backup = file; backup += ".bak";
    if (existed) {
        fs::remove(backup, ec); if (ec) return false;
        fs::rename(file, backup, ec); if (ec) return false;
    }
    fs::rename(tmp, file, ec);
    if (!ec) return true;
    if (existed) { std::error_code restore; fs::rename(backup, file, restore); }
    return false;
}
inline bool replaceChanged(fs::path const& file, std::string const& bytes) {
    std::error_code ec; auto size = fs::file_size(file, ec);
    return (!ec && size == bytes.size() && readFile(file) == bytes) || replaceFile(file, bytes);
}
inline std::string captureBytes(Recording const& r) {
    std::ostringstream out;
    out << "CFCAP 1 " << std::quoted(r.key) << ' ' << r.complete << ' ' << r.endTick << ' ' << std::quoted(r.backend)
        << ' ' << r.inputs.size() << ' ' << r.trace.size() << '\n' << std::setprecision(9);
    for (auto const& e : r.inputs) out << e.tick << ' ' << e.down << ' ' << e.button << ' ' << e.p1 << ' ' << e.x << ' ' << e.y << ' ' << e.mode << '\n';
    for (auto const& p : r.trace) out << p.tick << ' ' << p.x << ' ' << p.y << ' ' << p.x2 << ' ' << p.y2 << '\n';
    return out.str();
}
inline bool readCapture(std::istream& in, std::string const& key, Recording& result) {
    Recording r; std::string magic; int version; size_t count, samples;
    if (!(in >> magic >> version >> std::quoted(r.key) >> r.complete >> r.endTick >> std::quoted(r.backend) >> count >> samples) ||
        magic != "CFCAP" || version != 1 || r.key != key || r.endTick < 0 || count > 1000000 || samples > 2000000 ||
        (r.backend != "vanilla240" && r.backend != "cos240")) return false;
    int last = -1;
    for (size_t i = 0; i < count; ++i) {
        Input e;
        if (!(in >> e.tick >> e.down >> e.button >> e.p1 >> e.x >> e.y >> e.mode) || e.tick < 0 || e.tick < last ||
            (r.complete && e.tick > r.endTick) || e.button < 1 || e.button > 3 || e.mode < -1 || e.mode > 7 ||
            !std::isfinite(e.x) || !std::isfinite(e.y)) return false;
        last = e.tick; r.inputs.push_back(e);
    }
    last = -1;
    for (size_t i = 0; i < samples; ++i) {
        Sample p;
        if (!(in >> p.tick >> p.x >> p.y >> p.x2 >> p.y2) || p.tick < 0 || p.tick < last ||
            !std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.x2) || !std::isfinite(p.y2)) return false;
        last = p.tick; r.trace.push_back(p);
    }
    in >> std::ws; if (in.peek() != std::char_traits<char>::eof()) return false;
    r.windows.resize(r.inputs.size()); result = std::move(r); return true;
}
inline std::string playbackBytes(std::string const& digest, Recording const& r) {
    std::ostringstream out;
    out << "CFPLAY 1 " << digest << ' ' << r.verified << ' ' << r.endTick << ' ' << r.radius << ' ' << r.analysisPolicy
        << ' ' << std::quoted(r.backend) << ' ' << r.inputs.size() << '\n';
    for (size_t i = 0; i < r.inputs.size(); ++i) {
        auto w = i < r.windows.size() ? r.windows[i] : Window{};
        out << r.inputs[i].tick << ' ' << w.early << ' ' << w.late << ' ' << w.measured << ' ' << w.lowerBound << '\n';
    }
    return out.str();
}
inline bool readPlayback(std::istream& in, std::string const& digest, Recording& capture) {
    std::string magic, hash, backend; int version, end, radius, policy; bool verified; size_t count;
    if (!(in >> magic >> version >> hash >> verified >> end >> radius >> policy >> std::quoted(backend) >> count) ||
        magic != "CFPLAY" || version != 1 || hash != digest || end < 0 || radius < 1 || radius > 60 || policy < 0 || policy > 2 ||
        backend != capture.backend || count > capture.inputs.size() || (verified && !capture.complete)) return false;
    // Verification may drop trailing releases but must never silently discard presses.
    for (size_t i = count; i < capture.inputs.size(); ++i) if (capture.inputs[i].down) return false;
    std::vector<int> ticks; std::vector<Window> windows; int last = -1;
    for (size_t i = 0; i < count; ++i) {
        int tick; Window w;
        if (!(in >> tick >> w.early >> w.late >> w.measured >> w.lowerBound) || tick < 0 || tick < last ||
            (capture.complete && tick > end) || w.early < 0 || w.late < 0 || w.early > radius || w.late > radius) return false;
        ticks.push_back(tick); windows.push_back(w); last = tick;
    }
    in >> std::ws; if (in.peek() != std::char_traits<char>::eof()) return false;
    capture.inputs.resize(count);
    for (size_t i = 0; i < count; ++i) capture.inputs[i].tick = ticks[i];
    capture.windows = std::move(windows); capture.verified = verified; capture.endTick = end;
    capture.radius = radius; capture.analysisPolicy = policy; return true;
}
struct LoadedBundle { Recording recording; std::string captureDigest; bool analysisLoaded = false, backupAnalysis = false; };
inline bool loadBundle(fs::path const& directory, std::string const& key, LoadedBundle& result) {
    auto raw = readFile(directory / "capture.flr"); std::istringstream in(raw); LoadedBundle loaded;
    if (!readCapture(in, key, loaded.recording)) return false;
    loaded.captureDigest = fingerprint(raw);
    for (char const* name : {"playback.flp", "playback.flp.bak"}) {
        std::istringstream analysis(readFile(directory / name));
        if (readPlayback(analysis, loaded.captureDigest, loaded.recording)) {
            loaded.analysisLoaded = true; loaded.backupAnalysis = std::string(name) == "playback.flp.bak"; break;
        }
    }
    result = std::move(loaded); return true;
}
using BundleWriter = std::function<bool(fs::path const&, std::string const&)>;
inline bool saveBundle(fs::path const& directory, Recording const& r, bool captureDirty, std::string& digest,
                       BundleWriter const& writer = replaceChanged) {
    std::string capture, newDigest = digest;
    if (captureDirty) {
        capture = captureBytes(r); newDigest = fingerprint(capture);
        std::istringstream check(capture); Recording checked;
        if (!readCapture(check, r.key, checked)) return false;
    }
    if (newDigest.empty()) return false;
    // The hash binds both generations. Commit capture last; on interrupted writes,
    // the loader can still use playback.flp.bak matching the previous capture.
    if (!writer(directory / "playback.flp", playbackBytes(newDigest, r))) return false;
    if (captureDirty && !writer(directory / "capture.flr", capture)) return false;
    digest = newDigest; return true;
}
inline bool migrateLegacy(fs::path const& directory, std::string const& key, std::vector<fs::path> const& sources) {
    std::error_code ec; if (fs::exists(directory / "capture.flr", ec)) return false;
    for (auto const& source : sources) {
        std::ifstream in(source); Recording old;
        if (!read(in, key, old)) continue;
        std::string digest;
        return saveBundle(directory, old, true, digest);
    }
    return false;
}
}
