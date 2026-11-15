"""A5-3: near-clipped faces are written whenever the clip record arena has room, never past its end.

Compiles the production clip record size, space and write gate from
game/226/226_00_DrawLevelOvr1P.c against a small arena and checks them, then checks
that every clip record writer in the file goes through that gate before it moves the cursor.
The semantics follow CTR Native Expanded 209b71fed0 (no source-position window).
"""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
text = (root / "game/226/226_00_DrawLevelOvr1P.c").read_text()


def function(signature):
    start = text.index(signature + "\n{")
    return text[start:text.index("\n}\n", start) + 3]


def define(name):
    return re.search(name + r"\s*=\s*(0x[0-9A-Fa-f]+|\d+)", text).group(1)


fixture = r'''
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
typedef unsigned char u8;
typedef short s16;
#define DRAW_LEVEL_OVR1P_CLIP_RECORD_GT3_SIZE %s
#define DRAW_LEVEL_OVR1P_CLIP_RECORD_GT4_SIZE %s
struct DrawLevelOvr1PScratchVertex { s16 pos[3]; };
static u8 arena[512];
static u8 *gStart, *gCursor, *gEnd;
static u8 *DrawLevelOvr1P_GetClipRecordStart(void) { return gStart; }
static u8 *DrawLevelOvr1P_GetClipRecordCursor(void) { return gCursor; }
static u8 *DrawLevelOvr1P_GetClipRecordEnd(void) { return gEnd; }
%s
%s
%s
static void setup(size_t used, size_t size)
{
	gStart = arena;
	gCursor = arena + used;
	gEnd = arena + size;
}
int main(void)
{
	/* Far-away vertices: retail's window would have said no. */
	struct DrawLevelOvr1PScratchVertex far[4] = {{{30000, 30000, 30000}}, {{-30000, 30000, 30000}}, {{30000, -30000, 30000}}, {{-30000, -30000, 30000}}};
	int idx[4] = {0, 1, 2, 3};
	size_t gt3 = DRAW_LEVEL_OVR1P_CLIP_RECORD_GT3_SIZE, gt4 = DRAW_LEVEL_OVR1P_CLIP_RECORD_GT4_SIZE;

	assert(gt3 < gt4);
	assert(DrawLevelOvr1P_GetClipRecordSize(3) == gt3 && DrawLevelOvr1P_GetClipRecordSize(4) == gt4);

	/* Room: written even when every vertex is outside the old window. */
	setup(0, 512);
	assert(DrawLevelOvr1P_ShouldWriteRenderedClippedRecord(far, idx, 4) == 1);
	assert(DrawLevelOvr1P_ShouldWriteRenderedClippedRecord(far, idx, 3) == 1);

	/* Exactly one record left. */
	setup(512 - gt4, 512);
	assert(DrawLevelOvr1P_ShouldWriteRenderedClippedRecord(far, idx, 4) == 1);
	setup(512 - gt4 + 1, 512);
	assert(DrawLevelOvr1P_ShouldWriteRenderedClippedRecord(far, idx, 4) == 0);

	/* A GT3 fits where a GT4 does not. */
	setup(512 - gt3, 512);
	assert(DrawLevelOvr1P_ShouldWriteRenderedClippedRecord(far, idx, 3) == 1);
	assert(DrawLevelOvr1P_ShouldWriteRenderedClippedRecord(far, idx, 4) == 0);

	/* Full, or a broken arena: never write. */
	setup(512, 512);
	assert(DrawLevelOvr1P_ShouldWriteRenderedClippedRecord(far, idx, 3) == 0);
	gStart = NULL;
	assert(DrawLevelOvr1P_ShouldWriteRenderedClippedRecord(far, idx, 3) == 0);
	setup(600, 512);
	assert(DrawLevelOvr1P_ShouldWriteRenderedClippedRecord(far, idx, 3) == 0);

	puts("clip record space ok");
	return 0;
}
'''

source = fixture % (
    define("DRAW_LEVEL_OVR1P_CLIP_RECORD_GT3_SIZE"),
    define("DRAW_LEVEL_OVR1P_CLIP_RECORD_GT4_SIZE"),
    function("static size_t DrawLevelOvr1P_GetClipRecordSize(int count)"),
    function("static int DrawLevelOvr1P_HasClipRecordSpace(size_t size)"),
    function("static int DrawLevelOvr1P_ShouldWriteRenderedClippedRecord(const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int count)"),
)

with tempfile.TemporaryDirectory() as tmp:
    src = Path(tmp) / "t.c"
    exe = Path(tmp) / "t"
    src.write_text(source)
    subprocess.run(["cc", "-Wall", "-Wextra", "-o", str(exe), str(src)], check=True)
    subprocess.run([str(exe)], check=True)

# Every function that advances the clip record cursor must pass the gate first.
failures = []
bodies = re.split(r"\n(?=static [^\n;]*\()", text)
writers = 0
for body in bodies:
    if "DrawLevelOvr1P_SetClipRecordCursor(cursor + recordSize)" not in body:
        continue
    name = re.search(r"(\w+)\(", body.split("\n", 1)[0]).group(1)
    writers += 1
    gate = [g for g in ("DrawLevelOvr1P_ShouldWriteRenderedClippedRecord(", "DrawLevelOvr1P_HasClipRecordSpace(") if g in body]
    if not gate:
        failures.append(name)
    elif body.index(gate[0]) > body.index("record->header"):
        failures.append(name + " (gate after the first write)")
assert writers >= 6, writers
if failures:
    sys.exit("clip record writers without a space check: " + ", ".join(failures))
if "SourceInsideClipRecordWindow" in text:
    sys.exit("retail source-position window is back")
print("all %d clip record writers check space" % writers)
