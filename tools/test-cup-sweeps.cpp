// g++ -m32 -std=c++17 -DCTR_AP -Iap -Iap/vendor/json/include tools/test-cup-sweeps.cpp ap/ap_seedcfg.cpp -o /tmp/test-cup-sweeps
#include <cassert>
#include <cstdio>
#include <iostream>
#include <string>
#include <nlohmann/json.hpp>
#include "../ap/ap_seedcfg.h"
extern "C" void AP_LogLine(const char *) {}
using nlohmann::json;
static json seed() {
 json j={{"ctr_options",{{"schema_version",18},{"cup_sweep_checks",true}}},
         {"cup_sweep_checks",{{"version",1},{"cups",json::object()}}}};
 for(int c=0;c<5;c++)j["cup_sweep_checks"]["cups"][std::to_string(100+c)]={{"location",35028300+c},{"legs",{0,0,0,0}}};
 return j;
}
int main(int argc,char **argv){
 int vanilla[20]={3,9,2,5,6,14,12,10,4,8,1,11,0,15,7,13,6,5,1,7};
 if(argc>1 && std::string(argv[1])=="--probe"){
  ctr_cfg_set_vanilla_cup_legs(vanilla);std::string line;
  while(std::getline(std::cin,line)){
   ap_seedcfg_parse_json(json::parse(line));
   json result={{"active",ctr_cfg_active()!=0},{"enabled",ctr_cup_sweeps.enabled!=0},{"cups",json::object()}};
   for(int c=0;c<5;c++)if(ctr_cup_sweeps.present[c]){
    auto &r=result["cups"][std::to_string(100+c)];r["location"]=ctr_cup_sweeps.locations[c];r["legs"]=json::array();
    for(int l=0;l<4;l++)r["legs"].push_back(ctr_cup_sweeps.legs[c][l]);
   }
   std::cout<<"PROBE "<<result.dump()<<std::endl;
  }
  return 0;
 }
 int zeros[20]={};ctr_cfg_set_vanilla_cup_legs(zeros);
 auto j=seed();ap_seedcfg_parse_json(j);assert(ctr_cfg_active()&&ctr_cup_sweeps.enabled);
 for(int c=0;c<5;c++)assert(AP_CupSweepCode(&ctr_cup_sweeps,c)==35028300+c);
 for(int mutation=0;mutation<12;mutation++){
  auto b=j;
  switch(mutation){
   case 0:b["ctr_options"]["cup_sweep_checks"]=1;break;
   case 1:b.erase("cup_sweep_checks");break;
   case 2:b["cup_sweep_checks"]["version"]=true;break;
   case 3:b["cup_sweep_checks"]["version"]=2;break;
   case 4:b["cup_sweep_checks"]["extra"]=0;break;
   case 5:b["cup_sweep_checks"]["cups"]["100"]["location"]=35028301;break;
   case 6:b["cup_sweep_checks"]["cups"]["100"]["legs"]={0,0,0};break;
   case 7:b["cup_sweep_checks"]["cups"]["100"]["legs"][0]=true;break;
   case 8:b["cup_sweep_checks"]["cups"]["100"]["legs"][0]=1;break;
   case 9:b["cup_sweep_checks"]["cups"].erase("104");break;
   case 10:b["cup_sweep_checks"]["cups"]["105"]={};break;
   case 11:b["ctr_options"]["cup_sweep_checks"]=false;break;
  }
  ap_seedcfg_parse_json(b);assert(!ctr_cfg_active()&&!ctr_cup_sweeps.enabled);
 }
 ap_seedcfg_parse_json(j);assert(ctr_cup_sweeps.enabled);
 ap_seedcfg_parse_json(json::object());assert(!ctr_cup_sweeps.enabled);
 for(int c=0;c<5;c++)for(int pattern=0;pattern<16;pattern++)for(int forced=-1;forced<4;forced++){
  AP_CupSweepAttempt a={};AP_CupSweepEnter(&a,c);
  for(int leg=0;leg<4;leg++){
   AP_CupSweepStartLeg(&a,c,leg,1);
   AP_CupSweepRecord(&a,c,leg,(pattern>>leg)&1,leg==forced);
   AP_CupSweepRecord(&a,c,leg,(pattern>>leg)&1,leg==forced);
   assert(!AP_CupSweepReady(&a,c,1));
  }
  assert(AP_CupSweepReady(&a,c,4)==(pattern==15&&forced==-1));
 }
 AP_CupSweepAttempt a={};AP_CupSweepEnter(&a,0);
 AP_CupSweepStartLeg(&a,0,0,1);AP_CupSweepStartLeg(&a,0,0,1);assert(!a.active);
 AP_CupSweepEnter(&a,0);AP_CupSweepStartLeg(&a,0,1,1);assert(!a.active);
 AP_CupSweepEnter(&a,0);AP_CupSweepStartLeg(&a,1,0,1);assert(!a.active);
 std::puts("Cup sweep parser and 400 ordered-attempt scenarios passed");
}
