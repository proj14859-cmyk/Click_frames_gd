#pragma once
#include <string>
#include <cstdint>
#include <optional>

namespace cf {
struct TrimmedWave { std::string bytes; uint32_t silenceFrames=0, rate=0; };
inline std::optional<TrimmedWave> trimWave(std::string bytes) {
    auto u16=[&](size_t i){return uint16_t(uint8_t(bytes[i])) | uint16_t(uint8_t(bytes[i+1]))<<8;};
    auto u32=[&](size_t i){uint32_t n=0;for(int b=0;b<4;++b)n|=uint32_t(uint8_t(bytes[i+b]))<<(8*b);return n;};
    auto put=[&](size_t i,uint32_t n){for(int b=0;b<4;++b)bytes[i+b]=char((n>>(8*b))&255);};
    if(bytes.size()<12 || bytes.substr(0,4)!="RIFF" || bytes.substr(8,4)!="WAVE") return {};
    uint32_t rate=0; uint16_t align=0,bits=0;size_t data=0,size=0;
    for(size_t p=12;p+8<=bytes.size();) {
        auto n=u32(p+4); if(n>bytes.size()-p-8) return {};
        if(bytes.compare(p,4,"fmt ")==0) {
            if(n<16 || u16(p+8)!=1) return {};
            rate=u32(p+12);align=u16(p+20);bits=u16(p+22);
        }
        if(bytes.compare(p,4,"data")==0) {data=p+8;size=n;break;}
        p+=8+size_t(n)+(n&1);
    }
    if(!data || !rate || !align || (bits!=8 && bits!=16) || size%align) return {};
    size_t silence=0;
    while(silence+align<=size) {
        bool zero=true;for(size_t b=0;b<align;++b) if(uint8_t(bytes[data+silence+b])!=(bits==8?128:0)){zero=false;break;}
        if(!zero) break;
        silence+=align;
    }
    if(silence==size) return {}; // Do not turn an empty sound into a valid effect.
    uint32_t frames=uint32_t(silence/align);
    // Preserve RIFF alignment even for 8-bit mono.
    bytes.erase(data,silence);put(data-4,uint32_t(size-silence));
    if((silence&1)!=0) {
        if(size&1) { if(data+size-silence>=bytes.size())return {};bytes.erase(data+size-silence,1); }
        else bytes.insert(data+size-silence,1,'\0');
    }
    put(4,uint32_t(bytes.size()-8));
    return TrimmedWave{std::move(bytes),frames,rate};
}
}
