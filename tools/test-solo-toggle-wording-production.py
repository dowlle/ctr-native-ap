"""Play Solo toggle wording (A4-31): a toggle row is stored as the YAML "true" /
"false" but the options screen shows On / Off, the words the option descriptions
and the checklist rows use. Pins the display mapping and that the stored value,
the YAML writer and the toggle flip still use true / false. Run from the
repository root.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
solo = (root / "ap/ap_solo.cpp").read_text(encoding="utf-8")
logic = (root / "ap/ap_solo_logic.hpp").read_text(encoding="utf-8")

at = solo.index('extern "C" int AP_Solo_OptionGet(')
get = solo[at:solo.index("\n}\n", at)]
assert 'o.kind == "toggle"  ? (o.value == "true" ? "On" : "Off")' in get
# The row keeps its own string; only the copy shown on screen changes.
assert "o.value = " not in get

assert 'row.value = row.value == "true" ? "false" : "true";' in logic
assert 'return v.get<bool>() ? "true" : "false";' in logic
print("ok")
