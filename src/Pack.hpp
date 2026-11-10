#pragma once
#include <cctype>
#include <istream>
#include <ostream>
#include <string>
#include <set>
#include <vector>

// .cfpack: one level's runs in a single file you can send to someone.
//   CFPACK 1 <folder> <count>\n  then per file:  <name> <size>\n<bytes>\n
namespace cf {
constexpr size_t PACK_MAX_FILES = 64, PACK_MAX_BYTES = 32u * 1024 * 1024;

struct PackFile { std::string name, data; };
struct Pack { std::string folder; std::vector<PackFile> files; int version = 1; };

// Names in a pack become paths on disk: plain names only (no separators, no "..").
inline bool safeName(std::string const& n) {
    if (n.empty() || n.size() > 80 || n[0] == '.') return false;
    for (unsigned char c : n) if (!(std::isalnum(c) || c == '-' || c == '_' || c == '.')) return false;
    return true;
}
// CFPACK 1 carries run-<digits>.cfr. CFPACK 2 may also carry validated edit TSVs.
inline bool runName(std::string const& n) {
    if (!safeName(n) || n.size() < 9 || n.compare(0, 4, "run-") != 0 || n.compare(n.size() - 4, 4, ".cfr") != 0) return false;
    for (size_t i = 4; i + 4 < n.size(); ++i) if (!std::isdigit(static_cast<unsigned char>(n[i]))) return false;
    return true;
}
inline bool editName(std::string const& n) {
    if (!safeName(n) || n.compare(0, 6, "edits-") != 0 || n.size() < 13 || n.compare(n.size() - 4, 4, ".tsv") != 0) return false;
    auto dash = n.find('-', 6);
    if (dash == std::string::npos || dash == 6 || dash + 5 >= n.size()) return false;
    for (size_t i = 6; i < n.size() - 4; ++i) if (i != dash && !std::isdigit(static_cast<unsigned char>(n[i]))) return false;
    return true;
}
// Folder name for a level: readable name, then ID ("bloodbath-10565740").
inline std::string slug(std::string const& name) {
    std::string out;
    for (unsigned char c : name) {
        if (std::isalnum(c)) out += static_cast<char>(std::tolower(c));
        else if (!out.empty() && out.back() != '-') out += '-';
    }
    if (out.size() > 40) out.resize(40);
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out.empty() ? "level" : out;
}
inline void packWrite(std::ostream& out, Pack const& p) {
    out << "CFPACK " << p.version << ' ' << p.folder << ' ' << p.files.size() << '\n';
    for (auto const& f : p.files) out << f.name << ' ' << f.data.size() << '\n' << f.data << '\n';
}
// Packs come from other people: check everything, write nothing until it all parses.
inline bool packRead(std::istream& in, Pack& result) {
    Pack p; std::string magic; size_t count;
    if (!(in >> magic >> p.version >> p.folder >> count) || magic != "CFPACK" || (p.version != 1 && p.version != 2)
        || !safeName(p.folder) || count == 0 || count > PACK_MAX_FILES) return false;
    size_t total = 0;
    std::set<std::string> names;
    for (size_t i = 0; i < count; ++i) {
        PackFile f; size_t size;
        if (!(in >> f.name >> size) || (!runName(f.name) && !(p.version == 2 && editName(f.name))) || !names.insert(f.name).second || size > PACK_MAX_BYTES) return false;
        total += size;
        if (total > PACK_MAX_BYTES || in.get() != '\n') return false;
        f.data.resize(size);
        if (!in.read(f.data.data(), static_cast<std::streamsize>(size)) || in.get() != '\n') return false;
        p.files.push_back(std::move(f));
    }
    if (in.peek() != std::char_traits<char>::eof()) return false;
    result = std::move(p); return true;
}
}
