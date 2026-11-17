"""Run production difficulty setup through its stock grid overwrite and consumers."""
from pathlib import Path
import subprocess
import tempfile
from coop_fixture import function

root = Path(__file__).resolve().parents[1]
fixture = (root / 'tools/test-coop.c').read_text().split('int main(void)')[0]
fixture = fixture.replace('#include "../ap/', '#include "ap/')
bots = (root / 'game/BOTS.c').read_text()
fixture += bots[bots.index('enum\n{'):bots.index('\nforce_inline')]
fixture += r'''
int AP_AiDifficultyValue(void) { return 0; }
int AP_BossLossEasingNow(void) { return 0; }
int AP_HitFieldShortSeats(void) { return -1; }
void AP_RosterCompactSpawnOrder(unsigned char *order, int count)
{ (void)order; (void)count; assert(0); }
void AP_NavRec_AfterBotsInit(void) {}
void LIST_Clear(struct LinkedList *list) { (void)list; }
void BOTS_InitNavPath(struct GameTracker *gt, s16 index) { (void)gt; (void)index; }
void BOTS_SetGlobalNavData(u16 index) { (void)index; }
int RngDeadCoed(struct RngDeadCoedState *seed) { (void)seed; return 0; }
'''
for signature in ('int BOTS_Adv_NumTimesLostEvent(',
                  'internal void BOTS_Adv_LerpDifficulty(',
                  'internal void BOTS_Adv_CopySpawnOrder(',
                  'void BOTS_Adv_AdjustDifficulty('):
    fixture += function('game/BOTS.c', signature)
fixture += r'''
static void set_order(s32 *first, s32 *second, const unsigned char order[8])
{ memcpy(first, order, 4); memcpy(second, order + 4, 4); }
int main(void) {
    static struct GameTracker gt;
    static s16 interpolated[14];
    const unsigned char stock[8] = {1, 0, 2, 3, 4, 5, 6, 7};
    const unsigned char arcade[8] = {7, 6, 5, 4, 3, 2, 1, 0};
    sdata->gGT = &gt; gt.levelID = CRASH_COVE;
    sdata->arcade_difficultyParams = interpolated;
    set_order(&data.kartSpawnOrder.VS_2P_1, &data.kartSpawnOrder.VS_2P_2, stock);
    set_order(&data.kartSpawnOrder.arcade_1, &data.kartSpawnOrder.arcade_2, arcade);
    gt.gameMode1 = ADVENTURE_MODE; gt.numPlyrCurrGame = 2;
    memset(sdata->kartSpawnOrderArray, 0, 8);
    BOTS_Adv_AdjustDifficulty();
    assert(sdata->kartSpawnOrderArray[0] == 5 && sdata->kartSpawnOrderArray[1] == 4);
    unsigned seats = 0;
    for (int i = 0; i < 6; i++) seats |= 1u << sdata->kartSpawnOrderArray[i];
    assert(seats == 63);
    /* The actual path and launch consumers must see the final co-op order. */
    assert(sdata->driver_pathIndexIDs[0] == 1 && sdata->driver_pathIndexIDs[1] == 0);
    assert(sdata->accelerateOrder[0] == 7 && sdata->accelerateOrder[1] == 4);
    gt.gameMode1 = ARCADE_MODE;
    BOTS_Adv_AdjustDifficulty();
    assert(!memcmp(sdata->kartSpawnOrderArray, arcade, 8));
    gt.gameMode1 = ADVENTURE_MODE; gt.numPlyrCurrGame = 1;
    memcpy(sdata->kartSpawnOrderArray, stock, 8);
    BOTS_Adv_AdjustDifficulty();
    assert(!memcmp(sdata->kartSpawnOrderArray, stock, 8));
    puts("PASS production co-op grid: stock overwrite precedes six unique seats, humans behind bots, paths and launch use final order, solo and Arcade unchanged");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    src, binary = Path(tmp) / 'grid.c', Path(tmp) / 'grid'
    src.write_text(fixture)
    subprocess.run(['cc', '-m32', '-std=c99', '-DCTR_AP', '-DCTR_NATIVE', '-DBUILD=926',
                    '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
                    '-I', str(root), '-I', str(root / 'include'), '-I', str(root / 'ap'),
                    str(src), '-o', str(binary), '-lm'], check=True)
    subprocess.run([str(binary)], check=True)
