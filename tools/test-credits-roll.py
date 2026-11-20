#!/usr/bin/env python3
"""Credits roll lint and headless render (A5-6).

Runs tools/check-credits.sh (every shipped THIRD_PARTY_NOTICES component and the
fixed human credits are on the roll, glyph set and width), then renders the AP
section of ap/ap_credits_data.h the way AP_Credits_PrependScroll builds it
(header/entry colour code, centred in the 35-character field, one line per row)
and checks the result. The text render is written to $HARNESS_OUT (default
/tmp/ctr-harness) as credits-roll.txt so a reviewer can read what the roll
shows. It cannot draw the in-game font: that needs the Steam test row.
Run from the repository root.
"""
import os
import re
import subprocess
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
out_dir = Path(os.environ.get("HARNESS_OUT", "/tmp/ctr-harness"))
out_dir.mkdir(parents=True, exist_ok=True)

lint = subprocess.run(["bash", str(root / "tools/check-credits.sh")], capture_output=True, text=True)
sys.stdout.write(lint.stdout + lint.stderr)
assert lint.returncode == 0, "tools/check-credits.sh failed"

src = (root / "ap/ap_credits_data.h").read_text(encoding="utf-8")
width = int(re.search(r"#define AP_CREDITS_FIELD_WIDTH (\d+)", src).group(1))
src = subprocess.check_output(
    ["cc", "-E", "-P", "-DCTR_AP", "-I", str(root / "include"), "-x", "c", str(root / "ap/ap_credits_data.h")],
    text=True,
)
rows = [(int(h), t) for h, t in re.findall(r'\{([01]), "([^"]*)"\}', src)]
assert rows, "no credit rows parsed"

rendered = []
for header, text in rows:
    if not text:
        rendered.append("")
        continue
    pad = (width - len(text)) // 2
    rendered.append(("~00" if header else "~04") + " " * pad + text)

# Same invariants the hook relies on.
assert all(len(r.replace("~00", "").replace("~04", "")) <= width for r in rendered)
joined = "\n".join(rendered)
assert "ALWAYS JUMPING TRAP - ALIENGENIE" in joined
assert "Track and music by Lockheart" in joined
assert "~00" in joined and "~04" in joined

(out_dir / "credits-roll.txt").write_text(joined + "\n", encoding="utf-8")
print("rendered %d rows to %s" % (len(rendered), out_dir / "credits-roll.txt"))
