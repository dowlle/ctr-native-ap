// g++ -m32 -std=c++17 -DCTR_AP -I ap -I . -I include -I ap/vendor/json/include -o /tmp/test-crystal-checks tools/test-crystal-checks.cpp ap/ap_seedcfg.cpp
#include <cstdio>
#include <nlohmann/json.hpp>
#include "../ap/ap_seedcfg.h"
extern "C" void AP_LogLine(const char *) {}
static int checks,failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL line %d: %s\n",__LINE__,#x); } } while(0)
int main()
{
    // Pure pickup-address contract. Plan crystal ownership/admission and
    // runtime check emission are tested by the current content-plan suites.
    ctr_crystal_checks = {};
    ctr_crystal_checks.state=1;
    for(int a=0;a<4;++a) {
        ctr_crystal_checks.present[a]=1;
        for(int n=0;n<20;++n) ctr_crystal_checks.locations[a][n]=35028400+20*a+n;
    }
    int levels[4]={21,19,23,18};
    for (int a=0;a<4;++a) for (int p=1;p<=4;++p) {
        int base=AP_CrystalInstanceBase(a,p);
        for (int n=0;n<20;++n) CHECK(AP_CrystalPickupCode(&ctr_crystal_checks,levels[a],p,base+n)==35028400+20*a+n);
        CHECK(AP_CrystalPickupCode(&ctr_crystal_checks,levels[a],p,base-1)==-1);
        CHECK(AP_CrystalPickupCode(&ctr_crystal_checks,levels[a],p,base+20)==-1);
    }
    for(int n=0;n<80;++n) CHECK(AP_CrystalPseudoCode(&ctr_crystal_checks,52000+n)==35028400+n);
    CHECK(AP_CrystalPickupCode(&ctr_crystal_checks,20,1,8)==-1);
    CHECK(AP_CrystalPickupCode(&ctr_crystal_checks,21,0,8)==-1);
    CHECK(AP_CrystalPseudoCode(&ctr_crystal_checks,51999)==-1);
    CHECK(AP_CrystalPseudoCode(&ctr_crystal_checks,52080)==-1);
    ctr_crystal_checks.state=0;
    CHECK(AP_CrystalPickupCode(&ctr_crystal_checks,21,1,8)==-1);
    CHECK(AP_CrystalPseudoCode(&ctr_crystal_checks,52000)==-1);
    ctr_crystal_checks.state=-1;
    CHECK(AP_CrystalPickupCode(&ctr_crystal_checks,21,1,8)==-1);
    ctr_crystal_checks.state=1;ctr_crystal_checks.present[0]=0;
    CHECK(AP_CrystalPickupCode(&ctr_crystal_checks,21,1,8)==-1);
    std::printf("%d checks, %d failures\n",checks,failures);return failures?1:0;
}
