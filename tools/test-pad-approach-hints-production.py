"""Pad Approach Hints (#124): the production wiring. The pure decisions and
the once-ever latch run in test-pad-approach-hints.cpp; this pins that the pad
and garage draws call the hooks where their titles show, that pads use the
open glow list plus pickups, that the seed key is read only
from an admitted plan's envelope, and that the network sends a hint scout only
after the room's latch is read. It cannot run the engine or a server.
Run from the repository root.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
warp = (root / "game/232/AH_WarpPad.c").read_text(encoding="utf-8")
garage = (root / "game/232/AH_Garage.c").read_text(encoding="utf-8")
pads = (root / "ap/ap_hooks_pads.c").read_text(encoding="utf-8")
hooks = (root / "ap/ap_hooks.c").read_text(encoding="utf-8")
net = (root / "ap/ap_net.cpp").read_text(encoding="utf-8")
seed = (root / "ap/ap_seedcfg.cpp").read_text(encoding="utf-8")


def body(text, head):
    at = text.index(head)
    return text[at:text.index("\n}\n", at)]


# The call sits right after the pad name is drawn, once, and nowhere else.
name = warp.index("DecalFont_DrawLine(warppadLNG,")
call = warp.index("AP_PadApproachHints(levelID);")
# A4-59 computes the lines under the name before it; the next drawing after
# the call is the stage 2 line.
assert name < call < warp.index("if (apStage2 != AP_S2LINE_NONE)", call)
assert warp.count("AP_PadApproachHints(") == 1
assert '#include "ap_pad_hints_logic.h"' in hooks

# The hook: option, latch read, plan pad, occupied, content ready, open (racer
# lock included in AP_ContentPadOpen); then the glow enumeration with openOnly.
hook = body(pads, "void AP_PadApproachHints(int levelID)")
ready = body(pads, "static int AP_ApproachHintsReady(void)")
assert "ctr_cfg.pad_approach_hints" in ready and "ap_net_pad_hints_ready()" in ready
assert "AP_HintFilter_PadWaits(" in ready and "AP_ApproachHintsReady()" in hook
assert "AP_PadHint_Eligible(" in hook and "AP_ContentPadOpen(pad)" in hook and "pad->occupied" in hook
assert "AP_ContentPadReady(levelID)" in hook
assert "AP_ContentPadGlowBits(levelID, bits, codes, AP_PAD_DISPLAY_BITS_MAX, 1)" in hook
assert "ap_content_plan_sanity_pad(levelID, i," in hook
assert "ap_content_plan_sanity(pad->legs[i].track_id)" in hook
assert "AP_ApproachSendHints(levelID, codes, n)" in hook

garage_hook = body(pads, "void AP_BossGarageApproachHints(int hubID)")
assert "if (bossIsOpen)\n\t\t\tAP_BossGarageApproachHints(hubID);" in garage
assert garage.index("(char *)challenge,") < garage.index("AP_BossGarageApproachHints(hubID);")
assert "AP_ApproachHintsReady()" in garage_hook
assert "AP_OxideOffersFinalChallenge()" in garage_hook
assert "ap_content_plan_sanity_boss(boss)" in garage_hook
assert "ap_content_plan_sanity(leg->track_id)" in garage_hook
assert "AP_ApproachSendHints(" in garage_hook

filtered = body(pads, "static void AP_ApproachSendHints(")
assert "AP_LocationInLogic(codes[i])" in filtered
assert "AP_ContentCodeChecked(codes[i])" in filtered
assert "AP_HintFilter_Progression(ctr_cfg.pad_hints_progression_only," in filtered
assert "ap_net_pad_approach_hint(source, codes, eligible)" in filtered
logic = (root / "ap/ap_win_logic.c").read_text(encoding="utf-8")
in_logic = body(logic, "int AP_LocationInLogic(long long code)")
assert "AP_WinEnvNow()" in in_logic and "AP_PadHint_InLogic(&ctr_win_logic, &env, code)" in in_logic
racer = body((root / "ap/ap_hooks_gates.c").read_text(encoding="utf-8"), "int AP_ContentPadOpen(")
assert "AP_CharacterUnlocked(pad->racer_lock)" in racer

# Every glow bit carries its code: no bare push is left in the enumerator.
glow = body(pads, "static int AP_ContentPadGlowBits(")
assert "outBits[count++]" not in glow and glow.count("AP_GlowPush(") >= 8
assert "sanity" not in glow
# The glow and the display still pass no codes.
assert "AP_ContentPadGlowBits(destLevelID, outBits, NULL, cap, 0)" in pads
assert "AP_ContentPadGlowBits(destLevelID, outBits, NULL, cap, 1)" in pads

# Seed key: reset every parse, read only after an admitted plan.
assert "ctr_cfg.pad_approach_hints = 0;" in seed
assert '"pad_approach_hints"};' in seed
admitted = seed[seed.index("if (content_result == 2)\n\t\t{"):]
assert 'json_int(j["ctr_options"], "pad_approach_hints", 0)' in admitted[:600]

# Network: the latch is read at connect only when the option is on; the send
# filters through it, latches, and scouts with create_as_hint 2.
connect = net[net.index("g_pad_hints.connect("):]
assert connect.index("if (ctr_cfg.pad_approach_hints)") < connect.index("g_ap->Get({g_pad_hints.key});")
send = body(net, 'extern "C" int ap_net_pad_approach_hint(')
assert "ap_net_pad_hints_ready()" in send and "AP_PadHint_Select(" in send
assert send.index("g_ap->LocationScouts(locs, 2);") < send.index("g_pad_hints.mark(")
assert 'op.operation = "update";' in send
assert body(net, 'extern "C" int ap_net_pad_hints_ready(').count("g_pad_hints.ready()") == 1
assert net.count("g_pad_hints.disconnected();") == 3
assert "g_pad_hints.retrieved(" in net and "g_pad_hints.reply(value)" in net

# Run the production candidate builders with room, drawing and socket state
# stubbed. The latch and actual packet send are covered by the checks above.
start = pads.index("#define AP_APPROACH_HINT_CODES_MAX")
end = pads.index("// A4-59:", start)
fixture = r"""
#include <assert.h>
#include <stddef.h>
#include <string.h>
#include "ap_content_plan.h"
#include "ap_boss_won_flags.h"
#include "ap_hint_filters_logic.h"
#include "ap_pad_hints_logic.h"
#define AP_PAD_DISPLAY_BITS_MAX 256
#define CTR_CUSTOM_TRACKS

static struct {
    int pad_approach_hints, pad_hints_wait_for_spoiler, spoiler_items, pad_hints_progression_only;
} ctr_cfg = {1, 1, 1, 1};
static ctr_content_pad pad = {.occupied = 1}, boss_cup = {.cup = 1};
static ctr_content_sanity leg_rows[4], boss_rows[6];
static ctr_content_bosses bosses = {.present = 1};
static int preview, open = 1, final, ready = 1;
static int64_t sent[AP_PAD_DISPLAY_BITS_MAX];
static int sent_count, source;

static int ctr_cfg_active(void) { return 1; }
int ap_content_plan_active(void) { return 1; }
static int ap_net_pad_hints_ready(void) { return 1; }
static int AP_SpoilerPadPreviewShown(void) { return preview; }
static int AP_ContentPadReady(int id) { assert(id == 7); return ready; }
static int AP_ContentPadOpen(const ctr_content_pad *p) { assert(p == &pad); return open; }
const ctr_content_pad *ap_content_plan_pad(int id) {
    return id == 7 ? &pad : id == AP_CONTENT_GARAGE_CUP_PAD ? &boss_cup : NULL;
}
const ctr_content_bosses *ap_content_plan_bosses(void) { return &bosses; }
const ctr_content_sanity *ap_content_plan_sanity(const char *id) { return &leg_rows[id[0] - '0']; }
const ctr_content_sanity *ap_content_plan_sanity_boss(int boss) { return &boss_rows[boss]; }
int ap_content_plan_sanity_pad(int id, int family, int64_t *out, int capacity) {
    assert(id == 7 && capacity > 0);
    out[0] = 200 + family;
    return 1;
}
static int AP_ContentPadGlowBits(int id, int *bits, int64_t *codes, int cap, int only) {
    assert(id == 7 && bits && cap == AP_PAD_DISPLAY_BITS_MAX && only);
    codes[0] = 100;
    return 1;
}
static int AP_OxideOffersFinalChallenge(void) { return final; }
static int AP_ContentCodeChecked(int64_t code) { return code == 201; }
static int ap_net_scout_known(int64_t code, void *item, void *player, unsigned *flags) {
    assert(!item && !player);
    *flags = code == 202 ? 0 : AP_HINT_FLAG_PROGRESSION;
    return code != 302;
}
static int AP_LocationInLogic(int64_t code) { return code != 303; }
static int ap_net_pad_approach_hint(int id, const int64_t *codes, int count) {
    assert(count > 0 && count <= AP_PAD_DISPLAY_BITS_MAX);
    memcpy(sent, codes, count * sizeof *codes);
    source = id;
    return sent_count = count;
}
"""
cases = r"""
#define EXPECT(id, ...) do { \
    int64_t expected[] = {__VA_ARGS__}; \
    assert(source == (id) && sent_count == sizeof expected / sizeof *expected); \
    assert(!memcmp(sent, expected, sizeof expected)); \
    sent_count = 0; \
} while (0)

int main(void) {
    AP_PadApproachHints(7);
    AP_BossGarageApproachHints(1);
    assert(!sent_count);
    preview = 1;
    open = 0; AP_PadApproachHints(7); assert(!sent_count);
    open = 1; ready = 0; AP_PadApproachHints(7); assert(!sent_count);
    ready = 1; AP_PadApproachHints(7); EXPECT(7, 100, 200);
    ctr_cfg.pad_hints_progression_only = 0;
    AP_PadApproachHints(7); EXPECT(7, 100, 200, 202);
    ctr_cfg.pad_hints_progression_only = 1;
    pad.crystal = 1;
    AP_PadApproachHints(7); EXPECT(7, 100);
    pad.crystal = 0; pad.cup = 1;
    for (int i = 0; i < 4; ++i) {
        pad.legs[i].track_id[0] = boss_cup.legs[i].track_id[0] = '0' + i;
        pad.legs[i].track_pad = boss_cup.legs[i].track_pad = -1;
        leg_rows[i].counts[0] = 65536;
        leg_rows[i].locations[i % 3][i ? 0 : 79] = 300 + i;
    }
    AP_PadApproachHints(7); EXPECT(7, 100, 300, 301);
    ctr_cfg.pad_hints_progression_only = 0;
    AP_PadApproachHints(7); EXPECT(7, 100, 300, 301, 302);

    bosses.lap[0] = 400; bosses.wumpa[0] = 401;
    boss_rows[0].locations[1][0] = 402;
    AP_BossGarageApproachHints(1); EXPECT(AP_CONTENT_GARAGE_CUP_PAD, 35011100, 400, 401, 402);
    bosses.cup[0] = 1; boss_cup.legs[3].lap = 403; boss_cup.legs[3].wumpa = 404;
    AP_BossGarageApproachHints(1); EXPECT(AP_CONTENT_GARAGE_CUP_PAD, 35011100, 300, 301, 302, 403, 404);
    boss_rows[4].locations[0][0] = 405;
    boss_rows[5].locations[0][0] = 406;
    AP_BossGarageApproachHints(0); EXPECT(AP_CONTENT_GARAGE_CUP_PAD + 4, 35011104, 405);
    final = 1;
    AP_BossGarageApproachHints(0); EXPECT(AP_CONTENT_GARAGE_CUP_PAD + 4, 35011105, 406);
}
"""
with tempfile.TemporaryDirectory(prefix="ctr-approach-hints-") as folder:
    path = Path(folder)
    source_file = path / "hints.c"
    source_file.write_text(fixture + pads[start:end] + cases, encoding="utf-8")
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(root / "ap"),
                    str(source_file), "-o", str(path / "hints")], check=True)
    subprocess.run([str(path / "hints")], check=True)
print("pad approach hints production: ok")
