// g++ -m32 -std=c++17 -DCTR_AP -Iap -Iap/vendor/json/include tools/test-cup-sweeps.cpp ap/ap_seedcfg.cpp -o /tmp/test-cup-sweeps
#include <cassert>
#include <cstdio>
#include <iostream>
#include <string>
#include <nlohmann/json.hpp>
#include "../ap/ap_seedcfg.h"
extern "C" void AP_LogLine(const char *) {}
using nlohmann::json;
// The normal room's cup_sweep_checks reader was deleted. Keep the shared
// ordered-attempt state machine used by content-plan cups.
int main(){
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
 std::puts("Shared cup sweep: 400 ordered-attempt scenarios passed");
}
