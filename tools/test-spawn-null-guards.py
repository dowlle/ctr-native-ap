"""Issue #205: a failed INSTANCE_BirthWithThread must not be dereferenced.

On native the instance pool can be full, so the birth returns NULL. This scans
each hardened call site and requires a CTR_NATIVE NULL check on the returned
variable before its first use. Sites already guarded elsewhere are included so
a later edit cannot quietly drop them.

Run: python3 tools/test-spawn-null-guards.py
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parent.parent

# (file, birth marker substring, variable)
SITES = [
    ("game/231/RB_GenericMine.c", "RB_GenericMine_ThTick, sizeof(struct MineWeapon), 0);\n\n#if defined(CTR_NATIVE)", "instCrate"),
    ("game/231/RB_Crate.c", "0, 0);\n\n#if defined(CTR_NATIVE)", "explosionInst"),
    ("game/231/RB_MaskShieldCloud.c", "RB_MovingExplosive_ThTick, sizeof(struct TrackerWeapon), playerTh);\n\n#if defined(CTR_NATIVE)", "bombInst"),
    ("game/231/RB_MaskShieldCloud.c", "sizeof(struct RainCloud), d->instSelf->thread);\n\n#if defined(CTR_NATIVE)", "cloudInst"),
    ("game/231/RB_Explosion.c", "RB_Explosion_ThTick, 0, 0);\n\n#if defined(CTR_NATIVE)", None),
    ("game/Vehicle/VehTalkMask.c", "VehTalkMask_ThTick, 6, 0);\n\n#if defined(CTR_NATIVE)", "mhInst"),
]


def main():
    failed = 0
    for rel, marker, var in SITES:
        text = (ROOT / rel).read_text()
        n = text.count(marker)
        ok = n >= 1
        if ok and var:
            for m in re.finditer(re.escape(marker), text):
                tail = text[m.end():m.end() + 600]
                if not re.search(r"if \(%s == NULL\)" % var, tail):
                    ok = False
        if ok and var is None:
            for m in re.finditer(re.escape(marker), text):
                tail = text[m.end():m.end() + 600]
                if "== NULL" not in tail and "!= NULL" not in tail:
                    ok = False
        print("%s %s (%d site(s))" % ("PASS" if ok else "FAIL", rel, n))
        failed += 0 if ok else 1
    sys.exit(1 if failed else 0)


main()
