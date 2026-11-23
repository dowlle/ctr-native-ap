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
int ap_content_plan_active(void) { return 0; }
const ctr_content_pad *ap_content_plan_pad(int dest) { (void)dest; return NULL; }
'''
for signature in ("void LOAD_Robots1PRoster(", "void LOAD_Robots1P("):
    fixture += function("game/LOAD/LOAD_Assets.c", signature)
warp = (root / "game/232/AH_WarpPad.c").read_text()
grid_start = warp.index("\t// Retail repeats this setup")
grid_end = warp.index("\t// if flag is on-screen", grid_start)
fixture += r'''
static void pad_grid(struct GameTracker *gGT, struct Driver *driver, int levelID)
{
    int i, champID, champSlot, rng1, rng2;
    char randKartSpawn[8];
''' + warp[grid_start:grid_end] + "}\n"
for signature in ('int BOTS_Adv_NumTimesLostEvent(',
                  'internal void BOTS_Adv_LerpDifficulty(',
                  'internal void BOTS_Adv_CopySpawnOrder(',
                  'void BOTS_Adv_AdjustDifficulty('):
    fixture += function('game/BOTS.c', signature)
decal = (root / 'game/DecalMP.c').read_text()
fixture += decal[decal.index('struct DecalMPEntry'):decal.index('void DecalMP_02(')]
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
    assert(sdata->kartSpawnOrderArray[0] == 7 && sdata->kartSpawnOrderArray[1] == 6);
    unsigned seats = 0;
    for (int i = 0; i < 8; i++) seats |= 1u << sdata->kartSpawnOrderArray[i];
    assert(seats == 255);
    assert(sdata->driver_pathIndexIDs[0] == 2 && sdata->driver_pathIndexIDs[1] == 2);
    assert(sdata->accelerateOrder[0] == 5 && sdata->accelerateOrder[1] == 6);
    gridSize = 6;
    BOTS_Adv_AdjustDifficulty();
    assert(sdata->kartSpawnOrderArray[0] == 5 && sdata->kartSpawnOrderArray[1] == 4);
    seats = 0;
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
    // A previous solo load left Coco in the AI roster. She then joined the
    // split hub as P2, and the selected CTR Challenge will load with P1 only.
    LOAD_Robots1P(CRASH_BANDICOOT);
    data.characterIDs[1] = COCO_BANDICOOT;
    data.metaDataLEV[COCO_PARK].characterID_Champion = COCO_BANDICOOT;
    gt.gameMode1 = ADVENTURE_MODE | ADVENTURE_ARENA;
    gt.numPlyrCurrGame = 2;
    struct Driver padOwner = {0};
    for (int owner = 0; owner < 2; owner++) {
        padOwner.driverID = owner;
        pad_grid(&gt, &padOwner, COCO_PARK);
        assert(data.characterIDs[1] == COCO_BANDICOOT);
        seats = 0;
        for (int i = 0; i < 8; i++) seats |= 1u << sdata->kartSpawnOrderArray[i];
        assert(seats == 255 && sdata->kartSpawnOrderArray[3] == 0);
    }
    gt.numPlyrCurrGame = 1;
    padOwner.driverID = 0;
    pad_grid(&gt, &padOwner, COCO_PARK);
    assert(data.characterIDs[1] != COCO_BANDICOOT && sdata->kartSpawnOrderArray[3] == 0);
    gt.gameMode1 = ADVENTURE_MODE;
    static struct Driver field[8];
    static struct {
        struct Instance inst;
        struct InstDrawPerPlayer views[2];
    } karts[8];
    for (int i = 0; i < 8; i++) {
        gt.drivers[i] = &field[i];
        field[i].instSelf = &karts[i].inst;
    }
    gt.numPlyrCurrGame = 2;
    gt.cameraDC[0].pushBuffer = &gt.pushBuffer[0];
    gt.cameraDC[1].pushBuffer = &gt.pushBuffer[1];
    assert(!DecalMP_FieldFits(&gt));
    DecalMP_01(&gt);
    assert(gt.cameraDC[0].pushBuffer == &gt.pushBuffer[0]);
    assert(gt.cameraDC[1].pushBuffer == &gt.pushBuffer[1]);
    assert(karts[7].inst.flags == 0);
    gt.drivers[6] = gt.drivers[7] = NULL;
    assert(DecalMP_FieldFits(&gt));
    DecalMP_01(&gt);
    assert(karts[5].inst.flags & PIXEL_LOD);
    puts("PASS production co-op grid: eight seats use direct rendering; six-seat fallback, paths, launch, solo and Arcade preserved");
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
