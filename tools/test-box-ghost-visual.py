"""Issue #354: the unavailable AP box stands translucent and pays nothing.

Two things are checked, and they are the two halves of the feature:

  1. THE LOOK. The real AP_BoxGhostVisual body is lifted out of ap/ap_boxes.c
     and compiled against a fixture, so the flags, the alpha and the colour are
     asserted on the production statement list rather than on a copy. The exact
     values are the ones RB_CtrLetter.c writes for an unavailable Lettersanity
     letter, which is what the issue asked for; if the two ever drift apart this
     harness says so, because it reads the letter's numbers out of
     game/231/RB_CtrLetter.c rather than hard-coding them a second time.

  2. THE REFUSAL. A translucent box that still pays its check would be worse
     than the empty leg it replaced, so every break path in ap/ap_boxes.c is
     scanned for its AP_BoxPresentationCollectable guard: the kart-contact walk
     in AP_BoxesTick, the weapon fly-through, and the explosion sweep. The guard
     must also appear BEFORE the AP_BoxBreak call it protects.

Run: python3 tools/test-box-ghost-visual.py
"""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
boxes = (root / "ap/ap_boxes.c").read_text()
letter = (root / "game/231/RB_CtrLetter.c").read_text()

# ---------------------------------------------------------------------------
# The numbers the letter uses for "unavailable", read from production source.
# ---------------------------------------------------------------------------
unavailable = letter[letter.index("	else\n	{", letter.index("static void AP_CtrLetter_UpdateVisual")):]
unavailable = unavailable[: unavailable.index("\n	}")]
letter_alpha = int(re.search(r"alphaScale = (0x[0-9a-fA-F]+)", unavailable).group(1), 16)
assert letter_alpha == 0xA00, unavailable
assert "GHOST_DRAW_TRANSPARENT" in unavailable and "colorRGBA = 0;" in unavailable, unavailable

# ---------------------------------------------------------------------------
# 1. The production visual helper, compiled and called.
# ---------------------------------------------------------------------------
start = boxes.index("static void AP_BoxGhostVisual(struct Instance *inst)")
helper = boxes[start : boxes.index("\n}", start) + 2]

fixture = r"""
#include <assert.h>
#define DRAW_TRANSPARENT       1
#define GHOST_DRAW_TRANSPARENT 2
#define USE_SPECULAR_LIGHT     4
#define UNRELATED_FLAG         8
#define AP_PAD_GHOST_ALPHA 0xa00
struct Instance { int flags, alphaScale; unsigned colorRGBA; };
"""

tests = r"""
int main(void) {
 /* A solid crate as the spawn loader leaves it, and a crate that was already
    drawn opaque with a colour, so the clear is exercised as well as the set. */
 struct Instance a = {0, 0, 0};
 struct Instance b = {DRAW_TRANSPARENT|USE_SPECULAR_LIGHT|UNRELATED_FLAG, 0x1000, 0xfafafa0};
 AP_BoxGhostVisual(&a);
 assert(a.flags == GHOST_DRAW_TRANSPARENT);
 assert(a.alphaScale == LETTER_ALPHA);
 assert(a.colorRGBA == 0);
 AP_BoxGhostVisual(&b);
 assert((b.flags & (DRAW_TRANSPARENT|USE_SPECULAR_LIGHT)) == 0);
 assert(b.flags & GHOST_DRAW_TRANSPARENT);
 assert(b.flags & UNRELATED_FLAG);   /* unrelated engine flags survive */
 assert(b.alphaScale == LETTER_ALPHA);
 assert(b.colorRGBA == 0);
 /* Idempotent: it is written every frame. */
 AP_BoxGhostVisual(&b);
 assert(b.flags == (GHOST_DRAW_TRANSPARENT|UNRELATED_FLAG) &&
        b.alphaScale == LETTER_ALPHA && b.colorRGBA == 0);
 return 0;
}
"""

with tempfile.TemporaryDirectory() as tmp:
    src, exe = Path(tmp) / "fixture.c", Path(tmp) / "fixture"
    src.write_text(fixture + f"#define LETTER_ALPHA {letter_alpha:#x}\n" + helper + tests)
    subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-fsanitize=undefined",
                    str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)

# ---------------------------------------------------------------------------
# 2. Every break path refuses a ghost, and refuses it before it breaks anything.
# ---------------------------------------------------------------------------
GUARD = "if (!AP_BoxPresentationCollectable(s_boxPresent))"
paths = [
    ("static void AP_BoxesTick(", "AP_BoxBreak(gGT, i, inst);"),
    ("int AP_Boxes_OnWeaponMove(", "AP_BoxBreak(gGT, i, inst);"),
    ("void AP_Boxes_OnWeaponExplode(", "AP_BoxBreak(gGT, i, inst);"),
]
for anchor, breaker in paths:
    body = boxes[boxes.index(anchor):]
    body = body[: body.index("\n}\n")]
    assert GUARD in body, f"{anchor}: no collectable guard"
    assert body.index(GUARD) < body.index(breaker), f"{anchor}: guard is after the break"

# The helper is applied on the same walk that refuses the pickup: a visible box
# the player can drive through must never be a box that is drawn solid.
tick = boxes[boxes.index("static void AP_BoxesTick(") :]
tick = tick[: tick.index("\n}\n")]
assert "AP_BoxGhostVisual(inst);" in tick, "the tick never applies the ghost look"

# The rebuild must not stand a refused route down any more: NONE is the only
# value that spawns nothing, and it is reserved for the routes that own no boxes.
assert "if (!AP_BoxPresentationStands(s_boxPresent))" in boxes
assert "AP_BOX_PRESENT_GHOST" in boxes, "the rebuild never names the ghost case"

print("box ghost visual: production look matches the unavailable letter "
      f"(alpha {letter_alpha:#x}), and all 3 break paths refuse a ghost")
