#include "../src/Analysis.hpp"
#include "../src/Training.hpp"
#include "../src/Interop.hpp"
#include "../src/Diagnostics.hpp"
#include "../src/Audio.hpp"
#include <cassert>
#include <iostream>
int main(){
    cf::Observation base,cur;cf::CausalParity parity;
    base.flags[9]=291;cur.flags[9]=290;
    assert(parity.compare(base,cur,100));assert(parity.opened==100);
    assert(parity.compare(base,cur,101));cur.flags[9]=291;cur.values[0]=.049;
    assert(parity.compare(base,cur,102));assert(parity.opened<0 && parity.recovered==1);
    parity={};cur={};cur.flags[9]=290;assert(parity.compare(base,cur,100));assert(!parity.compare(base,cur,102));
    parity={};assert(parity.compare(base,cur,100));cur.flags[0]=1;assert(!parity.compare(base,cur,101));
    parity={};cur.flags[0]=0;cur.flags[9]=289;assert(!parity.compare(base,cur,100));
    parity={};cur.flags[9]=290;cur.values[0]=.05;assert(!parity.compare(base,cur,100));
    cur=base;cur.values[10]=90;assert(!cf::exactRejoin(base,cur));
    assert(cf::observationHash(base)!=cf::observationHash(cur));
    auto root=cf::fs::temp_directory_path()/"cf105-unit";cf::fs::remove_all(root);cf::fs::create_directories(root);
    cf::training::Features f;f.grounded=0;f.mode=4;f.speed=double(.7f);f.vy=-8.450123456789123;f.geometry=true;f.replayMatch=true;f.delta=17;f.dx=0.000000000000000123;f.dy=-2.312534918;
    std::vector<cf::training::Features> parsed;
    assert(cf::training::decode(cf::training::encode("123",{f}),"123",parsed));
    assert(parsed[0].vy==f.vy && parsed[0].dx==f.dx && parsed[0].speed==f.speed && parsed[0].replayMatch);
    assert(!cf::training::decode(cf::training::encode("123",{f}),"124",parsed));
    cf::Recording r;r.complete=r.verified=true;r.backend="vanilla240";r.endTick=40;
    r.inputs={{20,true,1,true},{22,false,1,true},{30,true,1,false}};r.windows={{1,1,true,false},{0,0,false,false},{3,4,true,true}};
    r.windows[0].collisionBounded=true;
    cf::training::Totals totals;auto bytes=cf::training::csv(r,{f,f,f},totals);
    assert(totals.exported==1 && totals.open==1 && bytes.starts_with(cf::training::header));
    cf::training::Totals openTotals; auto openBytes=cf::training::csv(r,{f,f,f},openTotals,"123",true);
    assert(openTotals.exported==1 && openTotals.open==1 && std::count(openBytes.begin(),openBytes.end(),'\n')==2);
    assert(openBytes.find(",3,4,1,")!=std::string::npos);
    assert(std::count(bytes.begin(),bytes.end(),'\n')==2);
    r.analysisPolicy=2;cf::training::csv(r,{f,f,f},totals);assert(totals.exported==0);r.analysisPolicy=0;
    r.complete=false;cf::training::csv(r,{f,f,f},totals);assert(totals.exported==0);r.complete=true;
    auto no=f;no.geometry=false;cf::training::csv(r,{no,f,f},totals);assert(totals.exported==1 && totals.geometry==1);
    no=f;no.replayMatch=false;cf::training::csv(r,{no,f,f},totals);assert(totals.exported==0 && totals.missing==1);
    no=f;no.vy=std::numeric_limits<double>::quiet_NaN();assert(cf::training::row(no,3).empty());
    auto shard=root/"training"/"123.csv";assert(cf::replaceFile(shard,bytes));
    assert(cf::replaceFile(root/"training"/"123.meta","CF_TRAINING 3\nPolicy: CF-ANALYSIS-10\n"));assert(cf::training::aggregate(root));
    assert(cf::readFile(root/"dataset_entrenamiento.csv")==bytes);assert(cf::training::aggregate(root));assert(cf::readFile(root/"dataset_entrenamiento.csv")==bytes);
    assert(cf::replaceFile(shard,cf::training::header));assert(cf::training::aggregate(root));assert(cf::readFile(root/"dataset_entrenamiento.csv")==cf::training::header);
    auto binary=cf::fwcBytes(r);assert(binary.size()==33 && binary.substr(0,4)=="FWC2");assert(uint8_t(binary[12])==1 && uint8_t(binary[16])==20 && uint8_t(binary[28])==1);
    auto json=cf::nandlJson(r);assert(json.find("\"timePosition\":20")!=std::string::npos && json.find("\"frameWindow\":3")!=std::string::npos);
    assert(cf::replaceFile(root/"error-log.txt",std::string(1024*1024,'x')));
    assert(cf::diagnostics::append(root/"error-log.txt","test",100,1,"FILE_READ","example\nerror"));
    assert(cf::fs::file_size(root/"error-log.txt")<=cf::diagnostics::logLimit);
    auto log=cf::readFile(root/"error-log.txt");assert(log.find("example error")!=std::string::npos);
    assert(cf::replaceFile(root/"crash.log","Stack Trace: test"));
    assert(cf::diagnostics::newestLog(root).filename()=="crash.log");assert(cf::diagnostics::newestLog(root/"absent").empty());
    auto wav=cf::trimWave(cf::readFile("resources/frame-1.wav"));assert(wav && wav->silenceFrames==1 && wav->rate==44100);
    assert(!cf::trimWave("RIFF").has_value());auto bad=cf::readFile("resources/frame-1.wav");bad.resize(35);assert(!cf::trimWave(bad));
    cf::fs::remove_all(root);std::cout<<"v1.0.5 parity, training, interop, diagnostics, audio tests passed\n";
}
