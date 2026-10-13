"""Source text of the ap_hooks unit, for the production harnesses.

ap/ap_hooks.c and its ap/ap_hooks_*.c parts are one translation unit:
game/game_unity.h includes them back to back (issue #305). A harness that
compiles production functions out of that unit reads it through read(), which
joins the parts in unity order without their per-file wrappers. The result is
the unit as one continuous text, so a function can move between parts, or sit
next to a part boundary, without breaking the harness.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# Each part opens with a header comment and "#ifdef CTR_AP"; every file but the
# last closes with "#endif // CTR_AP". ap_hooks.c also ends with a pointer to
# the parts.
PART_OPEN = "\n#ifdef CTR_AP\n\n"
PART_CLOSE = "#endif // CTR_AP\n"
HOOKS_POINTER = ("// The rest of the ap_hooks unit continues in the ap_hooks_*.c files that\n"
                 "// game/game_unity.h includes right after this one (issue #305).\n\n")


def files():
    """The unit's source files, in the order game/game_unity.h includes them."""
    unity = (ROOT / "game" / "game_unity.h").read_text(encoding="utf-8")
    names = re.findall(r'^#include "\.\./ap/(ap_hooks(?:_[a-z_]+)?\.c)"', unity, re.M)
    if not names or names[0] != "ap_hooks.c":
        raise RuntimeError("game/game_unity.h no longer includes ap/ap_hooks.c first")
    return [ROOT / "ap" / name for name in names]


def read():
    """The whole unit as one text: parts in unity order, wrappers removed."""
    paths = files()
    out = []
    for i, path in enumerate(paths):
        text = path.read_text(encoding="utf-8")
        if i > 0:
            text = text[text.index(PART_OPEN) + len(PART_OPEN):]
        if i < len(paths) - 1:
            text = text[:text.rindex(PART_CLOSE)]
            if i == 0:
                text = text.removesuffix(HOOKS_POINTER)
        out.append(text)
    return "".join(out)
