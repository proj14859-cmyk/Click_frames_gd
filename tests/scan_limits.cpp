#include <cstddef>
#include "../src/ScanLimits.hpp"
#include "../src/ScanSupport.hpp"
#include <cassert>
#include <array>
int main(){
    auto allowed=[](int now,bool dataset=false,bool audit=false,bool alive=true){
        return cf::reachedHorizon(now,100,2,600,true,dataset,false,audit,false,false,false,true,alive);
    };
    assert(!allowed(701));assert(allowed(702));
    assert(!allowed(702,true));assert(!allowed(702,false,true));assert(!allowed(702,false,false,false));
    assert(!cf::reachedHorizon(900,100,0,0,true,false,false,false,false,false,false,true,true));
    assert(!cf::missingValidationTrace(163,100,0));assert(cf::missingValidationTrace(164,100,0));
    assert(!cf::missingValidationTrace(1000,100,1));
    std::array<cf::AuditGate,8> classes{};
    classes[0].observed(false);assert(classes[0].disabled);assert(!classes[1].disabled);
}
