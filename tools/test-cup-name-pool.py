#!/usr/bin/env python3
"""The named cup pool in native (A3-42, nine tester cup names appended; A4-16, Dex Cup appended).

Reads ap/ap_content_registry.hpp (the registry the apworld's CUP_NAMES is synced
into by tools/sync-content-registry.py) and the engine's own font tables in
game/zGlobal_DATA.c, and pins four things:

1. The pool is append-only: the first 27 names and codes are the frozen 0.3.0
   ones, names 27 and up take the growth ranges 35027100 / 35027300, and the
   nine alpha3 names plus Dex Cup follow in order.
2. Every glyph of every name has an icon in data.font_characterIconID (0xFF is
   "not drawn"), underscore included.
3. The cup banner (UI_RaceFlow.c) and the standings title (UI_CupStandings.c)
   draw the raw name with FONT_BIG and no clamp, so the width is checked with
   the engine's DecalFont_GetLineWidthStrlen rules (17 px per character, 11 px
   for ':' and '.') and no name may be wider than the widest name already in
   the 0.3.0 pool, nor than one 512 px player view with a margin.
4. CTR_CFG_HIT_CONTENT_CUP_COUNT (the Hit Character per-cup order table) is at
   least the pool size, so a seed that draws any pool name connects.

Run from the repository root. The tracker shrinks a title to fit its box
(AP_TrackerText), so it cannot overflow and is not measured here.
"""
import json
import re
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
registry = json.loads(re.search(r'R"CTRREG\((.*)\)CTRREG"', (root / "ap/ap_content_registry.hpp").read_text(), re.S)[1])
data_c = (root / "game/zGlobal_DATA.c").read_text()
seedcfg = (root / "ap/ap_seedcfg.h").read_text()

failures = 0


def check(label, ok):
    global failures
    print(("ok   " if ok else "FAIL ") + label)
    failures += not ok


def table(name):
    body = re.search(r"\." + name + r"\s*=\s*\{(.*?)\n\s*\}", data_c, re.S)[1]
    return body


def font_icons():
    body = table("font_characterIconID")
    body = body[:body.index("#if BUILD != EurRetail")]
    out = []
    for line in body.splitlines():
        line = line.split("//")[0].strip().rstrip(",")
        if line:
            out.append(int(line, 0))
    return out


def widths(name):
    return [int(re.match(r"\s*(\d+)", l)[1]) for l in table(name).splitlines() if re.match(r"\s*\d+", l)]


icons = font_icons()
char_px, punc_px = widths("font_charPixWidth"), widths("font_puncPixWidth")
BIG = 1


def width(text):
    w = 0
    for c in text.encode():
        if c in b":.":
            w += punc_px[BIG] - char_px[BIG]
        if c > 2:
            w += char_px[BIG]
    return w


cups = registry["cups"]
names = [c["name"] for c in cups]
FROZEN = 27
FROZEN_NAMES = [
    "Patries Cup", "Icebound Cup", "Taor Cup", "JurnthReinal Cup", "Lockheart Cup",
    "Hypnoshark Cup", "Appie Cup", "TheCodingBob Cup", "Superstarxalien Cup",
    "TheRedhotbr Cup", "Avery Cup", "MarioSpore Cup", "Venusyprime Cup", "BEXUS99 Cup",
    "Bethany Cup", "Niko1Point0 Cup", "Aalhendi Cup", "MateusFavarin Cup",
    "TheUbMunster Cup", "Kabarakh Cup", "DragRedSim Cup", "Stroodlydoodles Cup",
    "Bob90 Cup", "Turbo Cup", "Relic Cup", "Multiworld Cup", "Sphere Cup",
]
NEW = ["Cristichi Cup", "FruitLion8 Cup", "TayWoodz Cup", "MeD Cup", "Lionboy Cup",
       "MisterShaokahn Cup", "Sturg Cup", "Prototyp_002 Cup", "Em Cup", "Dex Cup"]

check("pool has the 27 frozen names and the ten later names", len(cups) == FROZEN + len(NEW))
check("the 27 frozen names are unchanged and in order", names[:FROZEN] == FROZEN_NAMES)
check("the nine alpha3 names and Dex Cup follow Sphere Cup in order", names[FROZEN:] == NEW)
check("names are unique", len(set(names)) == len(names))
check("frozen names keep 35027000 + i and 35027200 + i",
      all(c["win"] == 35027000 + i and c["sweep"] == 35027200 + i for i, c in enumerate(cups[:FROZEN])))
check("later names use the growth ranges 35027100 + j and 35027300 + j",
      all(c["win"] == 35027100 + j and c["sweep"] == 35027300 + j for j, c in enumerate(cups[FROZEN:])))
check("pool codes never touch 35027027 (Cortex Vortex Cup Leg Victory)",
      all(c["win"] != 35027027 and c["sweep"] != 35027027 for c in cups))

bad = [(n, ch) for n in names for ch in n
       if not (0x21 <= ord(ch) < 0x21 + len(icons) and icons[ord(ch) - 0x21] != 0xFF) and ch != " "]
check("every glyph of every name has a font icon (" + ", ".join(sorted({c for _n, c in bad})) + " missing)" if bad
      else "every glyph of every name has a font icon, underscore included", not bad)
check("underscore has icon 0x2F", icons[ord("_") - 0x21] == 0x2F)

oldest = max(width(n) for n in names[:FROZEN])
rows = sorted(((width(n), n) for n in names), reverse=True)
for w, n in rows[:6]:
    print(f"     FONT_BIG {w:4d} px  {n}")
check(f"no name is wider than the widest frozen name ({oldest} px)", all(width(n) <= oldest for n in names[FROZEN:]))
check("every name fits one 512 px player view with 100 px to spare", all(width(n) <= 412 for n in names))
check("MisterShaokahn Cup is 18 characters and 306 px", width("MisterShaokahn Cup") == 306)

count = int(re.search(r"#define CTR_CFG_HIT_CONTENT_CUP_COUNT\s+(\d+)", seedcfg)[1])
check(f"Hit Character content cup table ({count}) covers the pool ({len(cups)})", count >= len(cups))

print(f"{'FAIL' if failures else 'PASS'} test-cup-name-pool ({failures} failures)")
sys.exit(1 if failures else 0)
