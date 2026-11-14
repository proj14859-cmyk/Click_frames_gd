#pragma once
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace cfc {
inline constexpr char schema[] = "CF-COLLECTOR-1";

struct InputEvent {
    int tick = 0, button = 1, mode = -1;
    bool down = false, p1 = true;
    float x = 0, y = 0;
};

struct Frame {
    int tick = 0;
    float percent = 0;
    double x1 = 0, y1 = 0, vy1 = 0, speed1 = 0;
    int mode1 = -1, gravity1 = 0, mini1 = 0, grounded1 = 0;
    double x2 = 0, y2 = 0, vy2 = 0, speed2 = 0;
    int mode2 = -1, gravity2 = 0, mini2 = 0, grounded2 = 0, dual = 0;
};

struct Attempt {
    std::string id, levelName, levelHash, result = "unknown";
    int64_t startedUnixMs = 0, endedUnixMs = 0;
    int levelID = 0, stars = 0, demon = 0, demonDifficulty = 0, length = 0, gameVersion = 0;
    int attemptNumber = 0, endTick = 0, deathObjectID = 0, deathObjectType = 0;
    float endPercent = 0, deathX = 0, deathY = 0;
    bool platformer = false, twoPlayer = false, completed = false, samplesTruncated = false;
    std::vector<InputEvent> inputs;
    std::vector<Frame> frames;
};

inline uint64_t fnv1a(std::string const& value) {
    uint64_t hash = 14695981039346656037ull;
    for (unsigned char c : value) { hash ^= c; hash *= 1099511628211ull; }
    return hash;
}
inline std::string jsonEscape(std::string const& value) {
    std::ostringstream out;
    for (unsigned char c : value) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 32) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << int(c) << std::dec;
                else out << char(c);
        }
    }
    return out.str();
}
inline std::string safeFolder(std::string value) {
    for (char& c : value) if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') c = '-';
    while (value.find("--") != std::string::npos) value.replace(value.find("--"), 2, "-");
    if (value.size() > 48) value.resize(48);
    if (value.empty()) value = "level";
    return value;
}
inline std::string metadata(Attempt const& a) {
    std::ostringstream out; out << std::setprecision(9);
    out << "{\n  \"schema\": \"" << schema << "\",\n"
        << "  \"attempt_id\": \"" << jsonEscape(a.id) << "\",\n"
        << "  \"level_id\": " << a.levelID << ",\n  \"level_name\": \"" << jsonEscape(a.levelName) << "\",\n"
        << "  \"level_hash\": \"" << jsonEscape(a.levelHash) << "\",\n"
        << "  \"stars\": " << a.stars << ",\n  \"demon\": " << a.demon << ",\n"
        << "  \"demon_difficulty\": " << a.demonDifficulty << ",\n  \"length\": " << a.length << ",\n"
        << "  \"game_version\": " << a.gameVersion << ",\n  \"platformer\": " << a.platformer << ",\n"
        << "  \"two_player\": " << a.twoPlayer << ",\n  \"attempt_number\": " << a.attemptNumber << ",\n"
        << "  \"started_unix_ms\": " << a.startedUnixMs << ",\n  \"ended_unix_ms\": " << a.endedUnixMs << ",\n"
        << "  \"result\": \"" << jsonEscape(a.result) << "\",\n  \"completed\": " << a.completed << ",\n"
        << "  \"end_tick\": " << a.endTick << ",\n  \"end_percent\": " << a.endPercent << ",\n"
        << "  \"death_object_id\": " << a.deathObjectID << ",\n  \"death_object_type\": " << a.deathObjectType << ",\n"
        << "  \"death_x\": " << a.deathX << ",\n  \"death_y\": " << a.deathY << ",\n"
        << "  \"input_events\": " << a.inputs.size() << ",\n  \"trajectory_samples\": " << a.frames.size() << ",\n"
        << "  \"samples_truncated\": " << a.samplesTruncated << "\n}\n";
    return out.str();
}
inline std::string inputsTsv(Attempt const& a) {
    std::ostringstream out; out << "tick\tdown\tbutton\tplayer\tx\ty\tmode\n" << std::setprecision(9);
    for (auto const& e : a.inputs) out << e.tick << '\t' << e.down << '\t' << e.button << '\t' << (e.p1 ? 1 : 2) << '\t' << e.x << '\t' << e.y << '\t' << e.mode << '\n';
    return out.str();
}
inline std::string trajectoryTsv(Attempt const& a) {
    std::ostringstream out; out << "tick\tpercent\tx1\ty1\tvy1\tspeed1\tmode1\tgravity1\tmini1\tgrounded1\tx2\ty2\tvy2\tspeed2\tmode2\tgravity2\tmini2\tgrounded2\tdual\n" << std::setprecision(17);
    for (auto const& f : a.frames) out << f.tick << '\t' << f.percent << '\t' << f.x1 << '\t' << f.y1 << '\t' << f.vy1 << '\t' << f.speed1 << '\t' << f.mode1 << '\t' << f.gravity1 << '\t' << f.mini1 << '\t' << f.grounded1 << '\t' << f.x2 << '\t' << f.y2 << '\t' << f.vy2 << '\t' << f.speed2 << '\t' << f.mode2 << '\t' << f.gravity2 << '\t' << f.mini2 << '\t' << f.grounded2 << '\t' << f.dual << '\n';
    return out.str();
}
}
