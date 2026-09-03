#!/usr/bin/env python3
"""Structural regression for issue #192's remaining feed behavior."""

from pathlib import Path


source = (Path(__file__).parents[1] / "ap" / "ap_hooks.c").read_text(encoding="utf-8")
# The held-position reason wording (issue #324) lives in the freestanding
# ap_rung_feed_reason_logic.h, which ap_hooks.c's AP_RungFeedReason now
# delegates to (tools/test-item-aliases.c pins it directly via a runtime call).
reason_source = (Path(__file__).parents[1] / "ap" / "ap_rung_feed_reason_logic.h").read_text(encoding="utf-8")

assert 'return "IN 1ST";' in reason_source
assert 'return "IN 3RD";' in reason_source
assert 'return "IN 5TH";' in reason_source
assert '"BE IN 1ST"' not in reason_source
assert '"BE IN 3RD"' not in reason_source
assert '"BE IN 5TH"' not in reason_source
assert 'return "FINISH ON PODIUM";' in reason_source
assert 'return "FINISH";' in reason_source
assert 'AP_FeedOnRungSent(code, rungTag);' in source
assert 'AP_FeedRememberSelfRung(item);' in source
assert 'if (AP_FeedConsumeSelfRung(item))' in source
assert 'if (playableRace && q->playCue)' in source
assert 'OtherFX_Play(0x41, 1);' in source
assert 'AP_FeedTickAndDraw(AP_FEED_X, AP_FEED_BASE_Y, 0);' in source
assert 'AP_FeedTickAndDraw(AP_FEED_X, AP_FEED_BASE_Y, 1);' in source

print("AP rung feed and race cue policy: PASS")
