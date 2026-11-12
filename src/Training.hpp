#pragma once
#include "Bundle.hpp"
#include <optional>
#include <locale>
#include <set>

namespace cf::training {
constexpr char legacyHeader[] = "vehiculo,velocidad_x,velocidad_y,gravedad,tamano,distancia_obs_x,distancia_obs_y,delta_ticks,TARGET_frames\n";
constexpr char v106Header[] = "vehiculo,velocidad_x,velocidad_y,gravedad,tamano,en_suelo,distancia_obs_x,distancia_obs_y,delta_ticks,TARGET_frames\n";
constexpr char v107Header[] = "vehiculo,velocidad_x,velocidad_y,gravedad,tamano,en_suelo,distancia_obs_x,distancia_obs_y,delta_ticks,TARGET_frames,early,late,abierta,radio,digest_nivel,digest_captura,indice_evento,tick,politica,tps\n";
constexpr char header[] = "vehiculo,velocidad_x,velocidad_y,gravedad,tamano,en_suelo,distancia_obs_x,distancia_obs_y,delta_ticks,TARGET_frames,early,late,abierta,radio,digest_nivel,digest_captura,indice_evento,tick,politica,tps,geometria,dual,modo2,y2,vy2,gravedad2\n";
// Persist exact pre-handleButton values separately; never reconstruct old runs.
struct Features {
    int mode=0, gravity=0, mini=0;
    double speed=0, vy=0, dx=0, dy=0;
    int64_t delta=0;
    bool geometry=false;
    bool replayMatch=false;
    int grounded=-1; // Legacy sidecars did not capture this; never infer it.
    int dual=-1, mode2=-1, gravity2=-1;
    double y2=0, vy2=0;
};
inline bool finite(Features const& f) {
    return f.mode>=0 && f.mode<=7 && (f.gravity==0 || f.gravity==1) && (f.mini==0 || f.mini==1) && f.delta>=0 &&
        std::isfinite(f.speed) && std::isfinite(f.vy) && std::isfinite(f.dx) && std::isfinite(f.dy);
}
inline std::string row(Features const& f, int width) {
    if (!finite(f) || f.grounded<0 || f.grounded>1 || width<1) return {};
    std::ostringstream out; out.imbue(std::locale::classic()); out << std::setprecision(std::numeric_limits<double>::max_digits10);
    out << f.mode << ',' << f.speed << ',' << f.vy << ',' << f.gravity << ',' << f.mini << ',' << f.grounded << ',';
    if(f.geometry) out << f.dx;
    out << ','; if(f.geometry) out << f.dy;
    out << ',' << f.delta << ',' << width << '\n';
    return out.str();
}
inline std::string encode(std::string const& digest, std::vector<Features> const& features) {
    std::ostringstream out; out.imbue(std::locale::classic()); out << "CFPHYS 3 " << digest << ' ' << features.size() << '\n' << std::setprecision(17);
    for (auto const& f : features) out << f.mode << ' ' << f.gravity << ' ' << f.mini << ' ' << f.speed << ' ' << f.vy << ' ' << f.dx << ' ' << f.dy << ' ' << f.delta << ' ' << f.geometry << ' ' << f.replayMatch << ' ' << f.grounded << ' ' << f.dual << ' ' << f.mode2 << ' ' << f.y2 << ' ' << f.vy2 << ' ' << f.gravity2 << '\n';
    return out.str();
}
inline bool decode(std::string const& bytes, std::string const& digest, std::vector<Features>& features) {
    std::istringstream in(bytes); in.imbue(std::locale::classic()); std::string magic, hash; int v; size_t count;
    if (!(in>>magic>>v>>hash>>count) || magic!="CFPHYS" || v<1 || v>3 || hash!=digest || count>1000000) return false;
    std::vector<Features> parsed(count);
    for (auto& f:parsed) {
        if (!(in>>f.mode>>f.gravity>>f.mini>>f.speed>>f.vy>>f.dx>>f.dy>>f.delta>>f.geometry>>f.replayMatch) || !finite(f)) return false;
        if(v>=2 && (!(in>>f.grounded) || f.grounded < -1 || f.grounded>1))return false;
        if(v>=3 && (!(in>>f.dual>>f.mode2>>f.y2>>f.vy2>>f.gravity2) || f.dual < -1 || f.dual>1 || !std::isfinite(f.y2) || !std::isfinite(f.vy2)))return false;
    }
    in>>std::ws; if (in.peek()!=std::char_traits<char>::eof()) return false;
    features=std::move(parsed); return true;
}
struct Totals { size_t evaluated=0, exported=0, open=0, geometry=0, missing=0,uncertified=0, incomplete=0, unverified=0, local=0; };
inline std::string csv(Recording const& r, std::vector<Features> const& f, Totals& totals, std::string const& captureDigest = {}, bool openOnly = false) {
    totals={}; std::string bytes=header;
    for(size_t i=0;i<r.inputs.size();++i) {
        if (!r.inputs[i].down || r.inputs[i].button!=1) continue;
        ++totals.evaluated;
        if(!r.complete){++totals.incomplete;continue;}
        if(!r.verified){++totals.unverified;continue;}
        if(r.analysisPolicy==2){++totals.local;continue;}
        if (i>=r.windows.size() || !r.windows[i].measured) { ++totals.missing; continue; }
        if (r.windows[i].lowerBound) { ++totals.open; if (!openOnly) continue; }
        else if (openOnly) continue;
        if (r.windows[i].estimated || (!openOnly && !r.windows[i].collisionBounded)) {++totals.uncertified;continue;}
        if (i>=f.size() || !finite(f[i]) || !f[i].replayMatch || f[i].grounded<0) { ++totals.missing; continue; }
        if (!f[i].geometry) ++totals.geometry;
        if (!r.complete || !r.verified || r.analysisPolicy==2) continue;
        auto line=row(f[i],r.windows[i].width()); if (!line.empty()) {
            line.pop_back(); auto const& w=r.windows[i];
            bytes+=line+","+std::to_string(w.early)+","+std::to_string(w.late)+","+std::to_string(w.lowerBound)+","+std::to_string(r.radius)+","+cf::fingerprint(r.key)+","+captureDigest+","+std::to_string(i)+","+std::to_string(r.inputs[i].tick)+","+std::to_string(r.analysisPolicy)+",240\n";
            ++totals.exported;
            bytes.pop_back();
            std::ostringstream extra; extra.imbue(std::locale::classic()); extra<<std::setprecision(17)<<','<<f[i].geometry<<',';
            if(f[i].dual>=0)extra<<f[i].dual;
            extra<<',';if(f[i].dual==1)extra<<f[i].mode2;
            extra<<',';if(f[i].dual==1)extra<<f[i].y2;
            extra<<',';if(f[i].dual==1)extra<<f[i].vy2;
            extra<<',';if(f[i].dual==1)extra<<f[i].gravity2;
            bytes+=extra.str()+"\n";
        }
    }
    return bytes;
}
// One authoritative shard per capture; rebuilding the aggregate makes retries
// idempotent and invalidation removes old labels instead of appending duplicates.
inline bool aggregate(fs::path const& root, bool openOnly = false) {
    auto directory = root/(openOnly ? "training-open" : "training");
    std::error_code ec; fs::create_directories(directory,ec); if(ec) return false;
    std::vector<fs::path> paths;
    for(fs::directory_iterator it(directory,ec),end;!ec && it!=end;it.increment(ec))
        if(it->path().extension()==".csv") paths.push_back(it->path());
    if(ec) return false;
    std::sort(paths.begin(),paths.end()); std::string all=header;
    for(auto const& p:paths) {
        auto bytes=readFile(p);
        if(bytes.starts_with(legacyHeader) || bytes.starts_with(v106Header) || bytes.starts_with(v107Header)) {
            auto archived=p;archived+=bytes.starts_with(legacyHeader)?".v105":bytes.starts_with(v106Header)?".v106":".v107";
            if(!fs::exists(archived,ec)){fs::rename(p,archived,ec);if(ec)return false;}
            continue;
        }
        if(!bytes.starts_with(header)) return false;
        if(all.size()+bytes.size()>MAX_BUNDLE_BYTES) return false;
        all+=bytes.substr(sizeof(header)-1);
    }
    return replaceChanged(root/(openOnly ? "dataset_abiertas.csv" : "dataset_entrenamiento.csv"),all);
}
struct DatasetSummary {
    size_t closed=0, open=0;
    std::set<std::string> levels;
    std::map<int,size_t> modes, widths;
};
inline std::vector<std::string> columns(std::string const& line) {
    std::vector<std::string> result; size_t start=0;
    for(size_t i=0;i<=line.size();++i) if(i==line.size() || line[i]==',') {
        result.push_back(line.substr(start,i-start)); start=i+1;
    }
    return result;
}
inline void summarizeCsv(std::string const& bytes, bool opened, DatasetSummary& out) {
    if(!bytes.starts_with(header)) return;
    std::istringstream in(bytes); std::string line; std::getline(in,line);
    while(std::getline(in,line)) {
        if(line.empty()) continue;
        auto c=columns(line); if(c.size()<20) continue;
        try {
            int mode=std::stoi(c[0]), width=std::stoi(c[9]);
            (opened?out.open:out.closed)++; out.modes[mode]++; out.widths[width]++;
            if(!c[14].empty()) out.levels.insert(c[14]);
        } catch(...) { /* malformed rows remain visible in their shard; omit from summary */ }
    }
}
inline bool refreshMetadata(fs::path const& root) {
    DatasetSummary s;
    summarizeCsv(readFile(root/"dataset_entrenamiento.csv"),false,s);
    summarizeCsv(readFile(root/"dataset_abiertas.csv"),true,s);
    std::ostringstream json;
    json << "{\n  \"schema\": 3,\n  \"closed_rows\": " << s.closed
         << ",\n  \"open_rows\": " << s.open << ",\n  \"unique_levels\": " << s.levels.size() << ",\n  \"modes\": {";
    bool first=true; for(auto const& [mode,count]:s.modes){if(!first)json<<',';first=false;json<<"\n    \""<<mode<<"\": "<<count;}
    if(!first) json<<'\n';
    json << "  },\n  \"widths\": {";
    first=true; for(auto const& [width,count]:s.widths){if(!first)json<<',';first=false;json<<"\n    \""<<width<<"\": "<<count;}
    if(!first) json<<'\n';
    json << "  }\n}\n";
    std::ostringstream report;
    report << "Dataset schema: 3\nClosed exact rows: " << s.closed << "\nOpen diagnostic rows: " << s.open
           << "\nUnique levels: " << s.levels.size() << "\nTraining readiness: "
           << (s.closed>=1000 && s.levels.size()>=10 ? "enough for an initial experiment" : "collect more exact rows from more levels") << "\n";
    report << "Modes:"; for(auto const& [mode,count]:s.modes) report << ' ' << mode << '=' << count; report << "\nWidths:";
    for(auto const& [width,count]:s.widths) report << ' ' << width << '=' << count;
    report << '\n';
    return replaceChanged(root/"dataset_manifest.json",json.str()) && replaceChanged(root/"dataset-report.txt",report.str());
}
}
