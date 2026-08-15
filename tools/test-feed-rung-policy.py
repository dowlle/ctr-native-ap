#!/usr/bin/env python3
"""Structural regression for issue #192's remaining feed behavior."""

from pathlib import Path


source = (Path(__file__).parents[1] / "ap" / "ap_hooks.c").read_text(encoding="utf-8")

assert 'return "BE IN 1ST";' in source
assert 'return "BE IN 3RD";' in source
assert 'return "BE IN 5TH";' in source
assert 'return "FINISH ON PODIUM";' in source
assert 'return "FINISH";' in source
assert 'AP_FeedOnRungSent(code, rungTag);' in source
assert 'AP_FeedRememberSelfRung(item);' in source
assert 'if (AP_FeedConsumeSelfRung(item))' in source
assert 'if (playableRace && q->playCue)' in source
assert 'OtherFX_Play(0x41, 1);' in source
assert 'AP_FeedTickAndDraw(AP_FEED_X, AP_FEED_BASE_Y, 0);' in source
assert 'AP_FeedTickAndDraw(AP_FEED_X, AP_FEED_BASE_Y, 1);' in source

print("AP rung feed and race cue policy: PASS")
