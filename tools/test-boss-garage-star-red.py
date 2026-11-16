"""#704: a collected boss garage star turns red on the hub map (vanilla colour).

Native #84 removed the star once the boss check was collected. The project ruled on
2026-11-16 that it turns red like vanilla instead.

1. Cut the marker-state tail out of game/232/AH_Map.c (from the "open, not
   beaten" default to the goto) and the colour pick out of the draw block, build
   both against stubs and check, for marker types 0..3 (Ripper Roo, Papu Papu,
   Komodo Joe, Pinstripe; type 3 is Pinstripe's Citadel garage, the one the AP
   branch used to remove) and the Oxide icon (type 4), with AP on and off:
   uncollected is state 1 (blue/white flicker on both timer frames), collected is
   state 2 (red, colour 3), never -1 (no star).
2. Check the source no longer sets the marker state to -1 for a beaten boss.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
src = (root / 'game/232/AH_Map.c').read_text()

a = src.index('\t\t\t\t\t// open, not beaten\n\t\t\t\t\tsVar8 = 1;')
b = src.index('goto LAB_800b17ec;', a)
tail = src[a:b]

c = src.index('if (sVar8 == 2)\n', src.index('// if beat boss race'))
d = src.index('\n\t\t\t\t// open, not beaten\n\t\t\t\tif (sVar8 == 1)', c)
colour = src[c:d]

assert 'sVar8 = -1' not in tail, 'beaten boss star must not be removed'
assert 'uVar6 = 3;' in colour

fixture = r'''
#include <stdio.h>
#define CTR_AP
typedef short s16;
static int cfg_on;
static struct { int timer; } gGT_s = { 0 }, *gGT = &gGT_s;
static int ctr_cfg_active(void) { return cfg_on; }
static int fails;
static void expect(const char *name, int got, int want)
{
    if (got != want) { printf("FAIL %s: got %d want %d\n", name, got, want); fails++; }
}
/* returns the marker state the production tail leaves in sVar8 */
static int state(s16 sVar1, s16 sVar7)
{
    s16 sVar8;
    int iVar5;
    (void)sVar1;
    TAIL
    return sVar8;
}
/* returns the colour the production draw block picks, or -1 for no marker */
static int colour_of(s16 sVar8)
{
    int uVar6 = 0;
    if (-1 < sVar8)
    {
        COLOUR
    }
    else return -1;
    return uVar6;
}
'''
fixture = fixture.replace('TAIL', tail).replace('COLOUR', colour)
main = r'''
int main(void)
{
    int on, type, frame;
    for (on = 0; on <= 1; on++)
    {
        cfg_on = on;
        for (type = 0; type <= 4; type++) /* 0..3 boss garages, 4 Oxide */
        {
            expect("uncollected -> open state", state(type, 0), 1);
            for (frame = 0; frame < 4; frame++)
            {
                gGT_s.timer = frame;
                expect("uncollected -> blue/white flicker",
                       colour_of(state(type, 0)), (frame & 2) ? 4 : 5);
            }
            expect("collected -> red state", state(type, 1), 2);
            expect("collected -> red colour 3", colour_of(state(type, 1)), 3);
        }
    }
    puts(fails ? "FAIL boss garage star red" : "ok boss garage star red");
    return fails != 0;
}'''
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    (tmp / 'star.c').write_text(fixture + main)
    subprocess.run(['cc', '-std=gnu99', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-function', '-Wno-unused-variable', '-Wno-unused-but-set-variable',
                    str(tmp / 'star.c'), '-o', str(tmp / 'star')], check=True)
    subprocess.run([str(tmp / 'star')], check=True)
