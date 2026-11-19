"""A4-27: a replayed ghost whose tape runs out on a track the AI cannot drive.

When a ghost reaches the end of its tape, GhostReplay_ThTick hands it to the
AI (BOTS_Driver_Convert) and drives it on with BOTS_ThTick_Drive. Convert
refuses a track with no nav path of two points, which is every custom track
without AI lines: it returns before it sets botData or ACTION_BOT. Driving the
ghost after a refused convert read a NULL nav frame (SIGSEGV, fault address
0xe), which crashed Retry in a custom Time Trial once the replayed tape ended.

This checks the production wiring:

1. The premise in game/BOTS.c: Convert returns before it touches botData when
   no path has two points, and ACTION_BOT is set only on the success path.
2. The ghost clears ACTION_BOT before the convert, so the bit afterwards says
   whether the convert happened.
3. BOTS_ThTick_Drive runs only under that test, with nothing between the
   convert and the test. A refused ghost is hidden, taken off the map and its
   thread parked on an empty tick, so the next frame cannot retry it.
4. Checkpoint relocation still recognizes the ghost after its tick changes.
5. No other caller drives a converted driver directly.
"""
from pathlib import Path
import os
import re
import subprocess
from tempfile import TemporaryDirectory

root = Path(__file__).resolve().parents[1]
bots = (root / 'game/BOTS.c').read_text()
ghost = (root / 'game/GhostReplay.c').read_text()
fails = []
checks = 0


def check(cond, what):
    global checks
    checks += 1
    if not cond:
        fails.append(what)


def body(src, signature):
    start = src.index(signature)
    open_brace = src.index('{', start)
    depth = 0
    for i in range(open_brace, len(src)):
        if src[i] == '{':
            depth += 1
        elif src[i] == '}':
            depth -= 1
            if depth == 0:
                return src[open_brace:i + 1]
    raise ValueError(signature)


# 1. The premise: a refused convert leaves the driver exactly as it was.
convert = body(bots, 'void BOTS_Driver_Convert(struct Driver *d)')
refuse = convert.index('if (navPathIndex == initialNavPathIndex)')
refuse_return = convert.index('return;', refuse)
check(convert.index('memset(&d->botData, 0, sizeof(struct BotData));') > refuse_return,
      'Convert refuses before it clears botData')
check(convert.index('d->botData.botNavFrame = firstNavFrame;') > refuse_return,
      'Convert refuses before it sets the nav frame')
check(convert.index('d->instSelf->thread->funcThTick = BOTS_ThTick_Drive;') > refuse_return,
      'Convert refuses before it swaps the tick')
sets_bot = [m.start() for m in re.finditer(r'\|\s*ACTION_BOT;', convert)]
check(len(sets_bot) == 1 and sets_bot[0] > refuse_return, 'ACTION_BOT is set once, on the success path')
check('numPoints' in convert[:refuse], 'the refusal is the two-point nav path test')

# 2 and 3. The ghost's end of tape.
tick = body(ghost, 'void GhostReplay_ThTick(struct Thread *t)')
end = tick.index('if (tape->ptrEnd <= (void *)packetPtr)')
end_block = body(tick[end:], 'if (tape->ptrEnd <= (void *)packetPtr)')
if 'if ((d->actionsFlagSet & ACTION_BOT) != 0)' not in end_block:
    print('FAIL: the end of tape drives the ghost without checking that the convert happened')
    raise SystemExit(1)
clear = end_block.index('d->actionsFlagSet &= ~ACTION_BOT;')
conv = end_block.index('BOTS_Driver_Convert(d);')
test = end_block.index('if ((d->actionsFlagSet & ACTION_BOT) != 0)')
drive = end_block.index('BOTS_ThTick_Drive(t);')
check(clear < conv < test < drive, 'clear, convert, test, then drive')
between = end_block[conv + len('BOTS_Driver_Convert(d);'):test]
check(not re.search(r'\S', between), 'nothing runs between the convert and its test')
check(len(re.findall(r'ACTION_BOT', end_block)) == 2, 'ACTION_BOT is only cleared and then tested at the end of tape')
check(end_block.count('BOTS_ThTick_Drive(') == 1, 'one drive call at the end of tape')
guarded = body(end_block[test:], 'if ((d->actionsFlagSet & ACTION_BOT) != 0)')
check('BOTS_ThTick_Drive(t);' in guarded, 'the drive call is inside the converted branch')
refused = body(end_block[test + len(guarded):], 'else')
check('inst->flags |= HIDE_MODEL;' in refused, 'a refused ghost is hidden')
check('d->ghostBoolInit = 0;' in refused, 'a refused ghost leaves the map')
map_src = (root / 'game/UI/UI_Map.c').read_text()
check('if (*(s16 *)((int)d + 0x632) == 0)' in body(map_src, 'void UI_Map_DrawGhosts('),
      'the map skips a ghost whose ghostBoolInit (0x632) is clear')
vehicle = (root / 'include/namespace_Vehicle.h').read_text()
check(re.search(r'// 0x632\s*\n\s*s16 ghostBoolInit;', vehicle) is not None, 'ghostBoolInit is the field at 0x632')
check('t->funcThTick = GhostReplay_ThTickParked;' in refused, 'a refused ghost is parked')
check(not re.search(r'\bBOTS_\w+\s*\(', refused), 'the refused branch calls no bot code')
check(end_block.rindex('d->actionsFlagSet |= ACTION_RACE_FINISHED;') > drive, 'both branches still mark the ghost finished')
check(end_block.rindex('t->flags |= THREAD_FLAG_DISABLE_COLLISION;') > drive, 'both branches still drop collision')

parked = body(ghost, 'static void GhostReplay_ThTickParked(struct Thread *t)')
check(re.sub(r'\s+', '', parked) == '{(void)t;}', 'the parked tick does nothing')
check(ghost.index('static void GhostReplay_ThTickParked') < ghost.index('void GhostReplay_ThTick(struct Thread *t)'),
      'the parked tick is declared before its use')

# 4. Relocation identifies ghost driver storage by the stable model index.
birth = body(ghost, 'void GhostReplay_Init1(void)')
checkpoint = (root / 'platform/native_checkpoint.c').read_text()
thread_object = body(checkpoint, 'internal void NativeCheckpoint_RelocateThreadObject(')
ghost_relocate = body(thread_object, 'if (thread->modelIndex == DYNAMIC_GHOST)')
check('t->modelIndex = DYNAMIC_GHOST;' in birth and 'modelIndex =' not in end_block,
      'the model index survives both end-of-tape branches')
check('NativeCheckpoint_RelocateDriver(oldHeader, liveHeader, driver);' in ghost_relocate,
      'a converted or parked ghost still gets driver pointer relocation')
check(0 <= ghost_relocate.find('NativeCheckpoint_RelocateDriver(')
      < ghost_relocate.find('NativeCheckpoint_RelocateGhostTape('),
      'the ghost tape is relocated after its driver pointer')
check('NATIVE_CHECKPOINT_FIELD_PTR(struct Driver, botData.botNavFrame)' in
      body(checkpoint, 'internal void NativeCheckpoint_RelocateDriver('),
      'relocation covers the ghost bot nav frame')
ghost_tape = body(checkpoint, 'internal void NativeCheckpoint_RelocateGhostTape(')
nav_ends = body(checkpoint, 'internal void NativeCheckpoint_RepairAuthoredNavEnds(')
check('header->last = NAVHEADER_GETFRAME(header) + header->numPoints;' in nav_ends,
      'a restored authored lane gets its end from the live header and frame count')

fixture = r"""
#include <stdint.h>
#include <string.h>
typedef short s16;
typedef unsigned char u8;
typedef unsigned int u32;
typedef struct { s16 x, y, z; } SVec3;
#define CTR_STATIC_ASSERT(expr)
#include "namespace_Ghost.h"
struct NavFrame { int word; };
struct NavHeader { s16 magicNumber, numPoints; struct NavFrame *last; };
struct Level { struct NavHeader **LevNavTable; };
struct GameTracker { struct Level *level1; unsigned int gameMode1; };
struct NativeCheckpointHeader { uintptr_t base; struct GhostTape *tape; };
static struct { struct NavHeader *NavPath_ptrHeader[3]; } fake_sdata;
#define sdata (&fake_sdata)
#define len(array) (sizeof(array) / sizeof((array)[0]))
#define GAME_CUTSCENE 1u
#define MAIN_MENU 2u
#define ADVENTURE_ARENA 4u
#define NAVHEADER_GETFRAME(header) ((struct NavFrame *)((header) + 1))

static int NativeCheckpoint_IsLivePointer(const struct NativeCheckpointHeader *header, const void *pointer)
{
    return pointer != NULL && pointer == header->tape;
}

static void NativeCheckpoint_RelocatePointerOrImageSlot(const struct NativeCheckpointHeader *old,
                                                        const struct NativeCheckpointHeader *live, void *slot)
{
    uintptr_t address;
    memcpy(&address, slot, sizeof address);
    address += live->base - old->base;
    memcpy(slot, &address, sizeof address);
}
"""
fixture += "static void NativeCheckpoint_RepairAuthoredNavEnds(struct GameTracker *gGT)\n" + nav_ends
fixture += ("\nstatic void NativeCheckpoint_RelocateGhostTape(const struct NativeCheckpointHeader *oldHeader, "
            "const struct NativeCheckpointHeader *liveHeader, struct GhostTape *tape, int active)\n" + ghost_tape)
fixture += r"""
static int check_replay_tape(void)
{
    struct Recording { struct GhostHeader header; u8 bytes[4]; };
    struct Recording saved = {0};
    struct Recording live = {.bytes = {1, 2, 3, 4}};
    struct GhostTape tape = {
        .gh = &saved.header,
        .gh_again = &saved.header,
        .ptrStart = saved.bytes,
        .ptrEnd = saved.bytes + sizeof saved.bytes,
        .ptrCurr = saved.bytes + 1,
        .packets = {{.bufferPacket = saved.bytes + 2}, {.bufferPacket = saved.bytes + sizeof saved.bytes}},
    };
    struct NativeCheckpointHeader oldHeader = {(uintptr_t)&saved, NULL};
    struct NativeCheckpointHeader liveHeader = {(uintptr_t)&live, &tape};
    NativeCheckpoint_RelocateGhostTape(&oldHeader, &liveHeader, &tape, 1);
    return tape.gh == &live.header && tape.gh_again == &live.header &&
           tape.ptrStart == live.bytes && tape.ptrEnd == live.bytes + sizeof live.bytes &&
           tape.ptrCurr == live.bytes + 1 && *(u8 *)tape.ptrCurr == 2 &&
           tape.packets[0].bufferPacket == live.bytes + 2 && *tape.packets[0].bufferPacket == 3 &&
           tape.packets[1].bufferPacket == tape.ptrEnd && tape.packets[2].bufferPacket == NULL;
}

int main(void)
{
    struct Lane { struct NavHeader header; struct NavFrame frames[3]; };
    struct Lane saved = {.header = {-0x1303, 3, NULL}};
    struct Lane live = {.header = {-0x1303, 3, saved.frames + 3}};
    struct NavHeader *table[3] = {NULL, &live.header, NULL};
    struct Level level = {table};
    struct GameTracker game = {&level, 0};
    NativeCheckpoint_RepairAuthoredNavEnds(&game);
    if (live.header.last != live.frames + 3)
        return 1;
    struct NavFrame *next = live.frames + 2;
    next++;
    if (next >= live.header.last)
        next = live.frames;
    return next != live.frames || !check_replay_tape();
}
"""
with TemporaryDirectory() as tmp:
    src = Path(tmp) / "ghost-wrap.c"
    exe = Path(tmp) / "ghost-wrap"
    src.write_text(fixture)
    subprocess.run([os.environ.get("CC", "cc"), "-std=c99", "-I", str(root / "include"),
                    str(src), "-o", str(exe)], check=True)
    check(subprocess.run([str(exe)]).returncode == 0,
          'restored tape bytes and authored lane ends follow the live addresses')

# 5. Only the ghost calls the drive tick by hand.
callers = []
for path in list((root / 'game').rglob('*.c')) + list((root / 'ap').rglob('*.c')):
    text = path.read_text(errors='replace')
    for m in re.finditer(r'\bBOTS_ThTick_Drive\s*\(\s*\w', text):
        if not text[:m.start()].rstrip().endswith('void'):
            callers.append(str(path.relative_to(root)))
check(callers == ['game/GhostReplay.c'], f'only GhostReplay.c drives a driver by hand (found {callers})')

for f in fails:
    print('FAIL:', f)
print(f'{checks - len(fails)}/{checks} ghost tape end checks passed')
raise SystemExit(1 if fails else 0)
