#!/usr/bin/env python3
"""Pin AP spawn-pointer invalidation to the instance-pool clear boundary."""

from pathlib import Path


SOURCE = Path(__file__).parents[1] / "game" / "MAIN" / "MainInit.c"


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1 : index]
    raise AssertionError(f"unterminated function: {signature}")


source = SOURCE.read_text(encoding="utf-8")
reset = function_body(source, "void MainInit_JitPoolsReset")
new = function_body(source, "void MainInit_JitPoolsNew")

clear = "JitPool_Clear(&gGT->JitPools.instance);"
hook = "AP_Spawn_OnPoolReset();"

assert reset.count(clear) == 1, "instance pool must be cleared exactly once"
assert reset.count(hook) == 1, "reset must invalidate AP spawn pointers exactly once"
assert reset.index(clear) < reset.index(hook), "invalidation must follow the pool clear"
assert hook not in new, "the hook must not be restricted to full pool initialization"
assert "#ifdef CTR_AP" in reset[: reset.index(hook)], "vanilla must not call the AP hook"

print("AP spawn pool-reset lifecycle: PASS")
