"""Production co-op predicates for tests that supply small engine structs."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def function(path, signature):
    source = (ROOT / path).read_text()
    start = source.index(signature)
    end = source.index('\n}\n', start) + 3
    return source[start:end]


def predicates(humans=False):
    # These two flags are macros outside GameMode1 in the engine headers.
    code = '\n#ifndef ADVENTURE_BOSS\n#define ADVENTURE_BOSS 0x80000000u\n#endif\n'
    code += '#ifndef TOKEN_RACE\n#define TOKEN_RACE 8u\n#endif\n'
    code += '#ifndef ACTION_BOT\n#define ACTION_BOT 0x100000u\n#endif\n'
    code += function('ap/ap_coop.h', 'static inline int AP_CoopHub(')
    code += function('ap/ap_coop.h', 'static inline int AP_CoopRace(')
    if humans:
        code += function('ap/ap_coop.h', 'static inline int AP_IsSlotHuman(')
    return code
