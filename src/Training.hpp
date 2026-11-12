#pragma once
#include "Bundle.hpp"
#include <optional>
#include <locale>

namespace cf::training {
constexpr char legacyHeader[] = "vehiculo,velocidad_x,velocidad_y,gravedad,tamano,distancia_obs_x,distancia_obs_y,delta_ticks,TARGET_frames\n";
constexpr char v106Header[] = "vehiculo,velocidad_x,velocidad_y,gravedad,tamano,en_suelo,distancia_obs_x,distancia_obs_y,delta_ticks,TARGET_frames\n";
constexpr char header[] = "vehiculo,velocidad_x,velocidad_y,gravedad,tamano,en_suelo,distancia_obs_x,distancia_obs_y,delta_ticks,TARGET_frames,early,late,abierta,radio,digest_nivel,digest_captura,indice_evento,tick,politica,tps\n";
// Persist exact pre-handleButton values separately; never reconstruct old runs.
struct Features {
    int mode=0, gravity=0, mini=0;
    double speed=0, vy=0, dx=0, dy=0;
    int64_t delta=0;
    bool geometry=false;
    bool replayMatch=false;
    int grounded=-1; // Legacy sidecars did not capture this; never infer it.
};
inline bool finite(Features const& f) {
    return f.mode>=0 && f.mode<=7 && (f.gravity==0 || f.gravity==1) && (f.mini==0 || f.mini==1) && f.delta>=0 &&
        std::isfinite(f.speed) && std::isfinite(f.vy) && std::isfinite(f.dx) && std::isfinite(f.dy);
}
inline std::string row(Features const& f, int width) {
    if (!finite(f) || !f.geometry || f.grounded<0 || f.grounded>1 || width<1) return {};
    std::ostringstream out; out.imbue(std::locale::classic()); out << std::setprecision(std::numeric_limits<double>::max_digits10);
    out << f.mode << ',' << f.speed << ',' << f.vy << ',' << f.gravity << ',' << f.mini << ',' << f.grounded << ',' << f.dx << ',' << f.dy << ',' << f.delta << ',' << width << '\n';
    return out.str();
}
inline std::string encode(std::string const& digest, std::vector<Features> const& features) {
    std::ostringstream out; out.imbue(std::locale::classic()); out << "CFPHYS 2 " << digest << ' ' << features.size() << '\n' << std::setprecision(17);
    for (auto const& f : features) out << f.mode << ' ' << f.gravity << ' ' << f.mini << ' ' << f.speed << ' ' << f.vy << ' ' << f.dx << ' ' << f.dy << ' ' << f.delta << ' ' << f.geometry << ' ' << f.replayMatch << ' ' << f.grounded << '\n';
    return out.str();
}
inline bool decode(std::string const& bytes, std::string const& digest, std::vector<Features>& features) {
    std::istringstream in(bytes); in.imbue(std::locale::classic()); std::string magic, hash; int v; size_t count;
    if (!(in>>magic>>v>>hash>>count) || magic!="CFPHYS" || (v!=1 && v!=2) || hash!=digest || count>1000000) return false;
    std::vector<Features> parsed(count);
    for (auto& f:parsed) {
        if (!(in>>f.mode>>f.gravity>>f.mini>>f.speed>>f.vy>>f.dx>>f.dy>>f.delta>>f.geometry>>f.replayMatch) || !finite(f)) return false;
        if(v==2 && (!(in>>f.grounded) || f.grounded < -1 || f.grounded>1))return false;
    }
    in>>std::ws; if (in.peek()!=std::char_traits<char>::eof()) return false;
    features=std::move(parsed); return true;
}
struct Totals { size_t evaluated=0, exported=0, open=0, geometry=0, missing=0,uncertified=0; };
inline std::string csv(Recording const& r, std::vector<Features> const& f, Totals& totals, std::string const& captureDigest = {}) {
    totals={}; std::string bytes=header;
    for(size_t i=0;i<r.inputs.size();++i) {
        if (!r.inputs[i].down || r.inputs[i].button!=1) continue;
        ++totals.evaluated;
        if (i>=r.windows.size() || !r.windows[i].measured) { ++totals.missing; continue; }
        if (r.windows[i].lowerBound) { ++totals.open; continue; }
        if (r.windows[i].estimated || !r.windows[i].collisionBounded) {++totals.uncertified;continue;}
        if (i>=f.size() || !finite(f[i]) || !f[i].replayMatch || f[i].grounded<0) { ++totals.missing; continue; }
        if (!f[i].geometry) { ++totals.geometry; continue; }
        if (!r.complete || !r.verified || r.analysisPolicy==2) continue;
        auto line=row(f[i],r.windows[i].width()); if (!line.empty()) {
            line.pop_back(); auto const& w=r.windows[i];
            bytes+=line+","+std::to_string(w.early)+","+std::to_string(w.late)+",0,"+std::to_string(r.radius)+","+cf::fingerprint(r.key)+","+captureDigest+","+std::to_string(i)+","+std::to_string(r.inputs[i].tick)+","+std::to_string(r.analysisPolicy)+",240\n";
            ++totals.exported;
        }
    }
    return bytes;
}
// One authoritative shard per capture; rebuilding the aggregate makes retries
// idempotent and invalidation removes old labels instead of appending duplicates.
inline bool aggregate(fs::path const& root) {
    std::error_code ec; fs::create_directories(root/"training",ec); if(ec) return false;
    std::vector<fs::path> paths;
    for(fs::directory_iterator it(root/"training",ec),end;!ec && it!=end;it.increment(ec))
        if(it->path().extension()==".csv") paths.push_back(it->path());
    if(ec) return false;
    std::sort(paths.begin(),paths.end()); std::string all=header;
    for(auto const& p:paths) {
        auto bytes=readFile(p);
        if(bytes.starts_with(legacyHeader) || bytes.starts_with(v106Header)) {
            auto archived=p;archived+=bytes.starts_with(legacyHeader)?".v105":".v106";
            if(!fs::exists(archived,ec)){fs::rename(p,archived,ec);if(ec)return false;}
            continue;
        }
        if(!bytes.starts_with(header)) return false;
        if(all.size()+bytes.size()>MAX_BUNDLE_BYTES) return false;
        all+=bytes.substr(sizeof(header)-1);
    }
    return replaceChanged(root/"dataset_entrenamiento.csv",all);
}
}
