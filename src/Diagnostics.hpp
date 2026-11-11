#pragma once
#include "Bundle.hpp"
#include <chrono>
#include <ctime>
#include <mutex>

namespace cf::diagnostics {
constexpr size_t logLimit = 256 * 1024;
inline std::string utc() {
    auto t = std::time(nullptr); std::tm value{};
#ifdef _WIN32
    gmtime_s(&value, &t);
#else
    gmtime_r(&t, &value);
#endif
    char text[32]; std::strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%SZ", &value); return text;
}
inline bool append(fs::path const& file, std::string phase, int tick, size_t event, std::string code, std::string message) noexcept {
    static std::mutex mutex;
    try {
        std::lock_guard lock(mutex);
        for (auto* text : {&phase, &code, &message}) for (char& c : *text) if (c == '\n' || c == '\r') c = ' ';
        auto line = "[" + utc() + "] [" + phase + "] [" + std::to_string(tick) + "] [" + std::to_string(event) + "] [" + code + "]: " + message + '\n';
        if (line.size() > logLimit / 2) {line.resize(logLimit / 2-1);line+='\n';}
        auto bytes = readFile(file);
        if (bytes.size() + line.size() > logLimit) {
            size_t cut = bytes.find('\n', bytes.size()>logLimit/2?bytes.size()-logLimit/2:bytes.size()/2);
            bytes = cut == std::string::npos ? std::string{} : bytes.substr(cut + 1);
        }
        return replaceFile(file, bytes + line);
    } catch (...) { return false; }
}
inline fs::path newestLog(fs::path const& directory) noexcept {
    try {
        fs::path newest; fs::file_time_type time{}; std::error_code ec;
        for (fs::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec)) {
            auto const& e = *it;
            if (!e.is_regular_file(ec) || ec || e.path().extension() != ".log") continue;
            auto t = e.last_write_time(ec);
            if (!ec && (newest.empty() || t > time)) { newest = e.path(); time = t; }
        }
        return newest;
    } catch (...) { return {}; }
}
inline std::string readLog(fs::path const& file, size_t limit = 4 * 1024 * 1024) noexcept {
    try {
        std::error_code ec; auto size = fs::file_size(file, ec);
        if (ec) return "No se pudo leer el registro.";
        if (size > limit) return "Registro demasiado grande para copiar (limite 4 MiB).";
        return readFile(file);
    } catch (std::exception const& e) { return std::string("Error de lectura: ") + e.what(); }
}
}
