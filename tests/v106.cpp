#include "../src/Analysis.hpp"
#include "../src/Training.hpp"
#include "../src/Interop.hpp"
#include "../src/Precision.hpp"
#include <cassert>
#include <iostream>
int main(){
    for(int radius=1;radius<=7;++radius)for(unsigned mask=0;mask<(1u<<(2*radius));++mask){
        std::vector<bool> truth(2*radius+1,true);int bit=0;
        for(int i=0;i<=2*radius;++i)if(i!=radius)truth[i]=(mask>>(bit++))&1;
        cf::GroupedSearch scan;scan.reset(radius);std::array<bool,41> seen{};int calls=0;
        while(!scan.done){assert(!seen[20+scan.offset]);seen[20+scan.offset]=true;scan.accept(truth[radius+scan.offset]);assert(++calls<=2*radius);}
        auto a=scan.window(),b=cf::measure(truth,radius);assert(a.early==b.early && a.late==b.late && a.lowerBound==b.lowerBound && !a.estimated);
    }
    cf::GroupedSearch scan;scan.approximate=true;scan.reset(20);int calls=0;
    while(!scan.done){scan.accept(true);++calls;}assert(calls==8 && scan.window().estimated && scan.window().lowerBound && scan.window().width()==41);
    scan.approximate=false;scan.reset(20);calls=0;while(!scan.done){scan.accept(scan.offset!=7 && scan.offset!=-2);++calls;}
    assert(scan.window().early==1 && scan.window().late==6 && !scan.window().lowerBound && calls<40);
    cf::CausalEvent a{100,2,45,1,9},b=a;b.tick=110;
    assert(!cf::timedCause(a,b,0) && cf::timedCause(a,b,8));b.object=46;assert(!cf::timedCause(a,b,8));
    assert(cf::shiftedLaunch(100,97,-3) && !cf::shiftedLaunch(100,100,-3));
    assert(std::abs(cf::precision::windowMs(3)-12.5)<1e-12 && std::abs(cf::precision::windowMs(1)-1000./240)<1e-12);
    cf::Recording r;r.key="test";r.complete=r.verified=true;r.radius=20;r.endTick=100;r.inputs={{50,true,1,true,1,2,0}};r.windows={{1,1,true,false,true,false}};
    std::ostringstream packed;cf::write(packed,r);cf::Recording loaded;std::istringstream in(packed.str());assert(cf::read(in,r.key,loaded) && loaded.windows[0].estimated);
    auto playback=cf::playbackBytes("123",r);auto cap=r;std::istringstream play(playback);assert(cf::readPlayback(play,"123",cap) && cap.windows[0].estimated);
    assert(cf::closedWindows(r).empty());
    cf::training::Features f;f.mode=0;f.speed=.9f;f.vy=8.450123456789;f.geometry=f.replayMatch=true;f.grounded=1;
    cf::training::Totals totals;cf::training::csv(r,{f},totals);assert(totals.exported==0 && totals.uncertified==1);
    r.windows[0].estimated=false;r.windows[0].collisionBounded=true;auto csv=cf::training::csv(r,{f},totals);assert(totals.exported==1);
    auto line=csv.substr(csv.find('\n')+1);assert(std::count(line.begin(),line.end(),',')==25);
    auto root=cf::fs::temp_directory_path()/"cf106-test";cf::fs::remove_all(root);cf::fs::create_directories(root);
    assert(cf::replaceFile(root/"training"/"old.csv",cf::training::legacyHeader));assert(cf::training::aggregate(root));
    assert(cf::fs::exists(root/"training"/"old.csv.v105") && cf::readFile(root/"dataset_entrenamiento.csv")==cf::training::header);
    assert(cf::replaceFile(root/"training"/"current.csv",csv));assert(cf::training::aggregate(root));
    assert(cf::training::aggregate(root,true));assert(cf::training::refreshMetadata(root));
    auto manifest=cf::readFile(root/"dataset_manifest.json");auto report=cf::readFile(root/"dataset-report.txt");
    assert(manifest.find("\"closed_rows\": 1")!=std::string::npos && manifest.find("\"unique_levels\": 1")!=std::string::npos);
    assert(report.find("Closed exact rows: 1")!=std::string::npos);
    cf::fs::remove_all(root);
    std::cout<<"v1.0.6 exact/estimated search, causal timing, latch, ms, persistence and training tests passed\n";
}
