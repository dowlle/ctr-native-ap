"""Compile actual Oxide win notification; entry/loss never invoke this hook."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "ap/ap_hooks.c").read_text()
start = source.index("void AP_NotifyGoal(int oxideSecond)")
end = source.index("\n// ---------------------------------------------------------------------------", start)
function = source[start:end]
reward_source = (root / "game/222.c").read_text()
start = reward_source.index("\t\t\tadv->storyFlags |= ADV_REWARD_OXIDE_FIRST_WIN_FLAGS;")
end = reward_source.index("\n#ifdef CTR_AP\n\t\t\tif (AP_GoalClaimOxideEnding())", start)
reward = reward_source[start:end]
fixture = r'''
#include <assert.h>
#define AP_GOAL_BIT_OXIDE_FIRST 115
#define AP_GOAL_BIT_OXIDE_SECOND 116
static struct { int goal_oxide, oxide_1_optional; } ctr_cfg;
static int active, ap_oxide_first_beaten, ap_oxide_final_beaten;
static int first, final, arm, evaluate;
#define AA_OXIDE_SECOND_WIN_BOSS_ID 7
#define ADV_REWARD_OXIDE_FIRST_WIN_FLAGS 1
#define ADV_REWARD_OXIDE_SECOND_WIN_FLAGS 2
static struct { int bossID; } game, *gGT=&game;
static struct { int storyFlags; } progress, *adv=&progress;
int ctr_cfg_active(void) { return active; }
void AP_GoalArmLiveEvent(void) { arm++; }
void AP_NotifyAdvReward(int bit) {
 if (bit == 115) first++; else { assert(bit == 116); final++; }
}
void AP_EvaluateGoal(void) { evaluate++; }
'''
tests = r'''
int main(void) {
 for (active=0; active<2; active++)
 for (int goal=0; goal<4; goal++)
 for (int enabled=0; enabled<3; enabled++)
 for (int second=0; second<2; second++) {
  ctr_cfg.goal_oxide=goal; ctr_cfg.oxide_1_optional=enabled;
  first=final=arm=evaluate=ap_oxide_first_beaten=ap_oxide_final_beaten=0;
  AP_NotifyGoal(second);
  assert(first==(!second || (active && goal==2 && enabled)));
  assert(final==second && arm==1 && evaluate==1);
  assert(ap_oxide_first_beaten==!second && ap_oxide_final_beaten==second);
  first=final=arm=evaluate=ap_oxide_first_beaten=ap_oxide_final_beaten=0;
  game.bossID=second ? 7 : 6;
  RewardOxide();
  int skip = active && goal==2 && enabled;
  assert(first==1 && final==second);
  assert(arm==(second && !skip ? 2 : 1) && evaluate==arm);
  assert(ap_oxide_first_beaten==(!second || !skip));
  assert(ap_oxide_final_beaten==second);
 }
 return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    src, exe = Path(tmp) / "fixture.c", Path(tmp) / "fixture"
    src.write_text(fixture + function + "\nvoid RewardOxide(void) {\n" + reward + "\n}\n" + tests)
    subprocess.run(["cc", "-std=c99", "-DCTR_AP", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=undefined", str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("PASS: 48 isolated win notifications plus 48 actual reward-path sequences, UBSan; no opt-in duplicate dispatch")
