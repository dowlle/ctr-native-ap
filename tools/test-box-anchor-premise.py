#!/usr/bin/env python3
"""Pin the engine premises the AP crate spawn-height correction rests on.

The correction in ap/ap_box_offset_logic.h lifts a crate by its own measured
base offset because of two claims about the ENGINE, neither of which any C
harness can reach:

  1. an authored placement row is a GROUND point, because Driver.posCurr is the
     kart's ground contact point, and
  2. one model unit is headerScale/0x1000 world units, because the renderer
     feeds ((frame.pos + vertexByte) << 2) to the GTE with the header scale
     loaded as (scale >> 2), pairing frame pos.y with the VERTICAL vertex byte.

Both are read out of the decompiled sources here, so a decomp correction that
moves either one fails this check instead of silently making every AP crate
float or sink. Lessons Learned 16: a mechanism is verified only against the data
it claims to describe.

    python3 tools/test-box-anchor-premise.py
"""

from pathlib import Path
import re


ROOT = Path(__file__).parents[1]
COLL = (ROOT / "game" / "COLL.c").read_text(encoding="utf-8")
VEH_BIRTH = (ROOT / "game" / "Vehicle" / "VehBirth.c").read_text(encoding="utf-8")
VEH_FORCE = (ROOT / "game" / "Vehicle" / "VehPhysForce.c").read_text(encoding="utf-8")
BOTS = (ROOT / "game" / "BOTS.c").read_text(encoding="utf-8")
QUEUE = (ROOT / "game" / "RenderBucket" / "RenderBucket_QueueExecute.c").read_text(encoding="utf-8")


def one_int(source: str, pattern: str, what: str) -> int:
    found = re.findall(pattern, source)
    assert len(found) == 1, f"{what}: expected exactly one match, found {len(found)}"
    return int(found[0], 0)


# ── premise 1: Driver.posCurr is the kart's ground contact point ─────────────

# The swept driver collision is a sphere of COLL_MOVED_PLAYER_HIT_RADIUS centred
# on posCurr + originToCenter. When the radius equals the centre offset, the
# sphere's lowest point IS the origin, so a resting kart's origin sits on the
# road rather than inside it.
sweep_radius = one_int(
    COLL, r"COLL_MOVED_PLAYER_HIT_RADIUS = (0x[0-9a-fA-F]+|\d+),", "swept player hit radius"
)
centre_offset = one_int(
    VEH_FORCE,
    r"originToCenter = VehPhysForce_OnApplyForces_RotateVector\(&driver->matrixFacingDir, 0, (\d+), 0\)",
    "driver origin-to-centre offset",
)
assert sweep_radius == centre_offset, (
    "the swept driver sphere must sit exactly one radius above the origin "
    f"(radius {sweep_radius}, centre offset {centre_offset}); if these differ, a placement "
    "row is no longer a ground point and the crate lift needs re-deriving"
)

# The same geometry from the other side: a racer is born with its origin AT the
# quadblock hit position, and the race-start call passes no vertical offset.
assert (
    "d->posCurr.y = CTR_MipsSll(CTR_MipsAddLo(sps->Union.QuadBlockColl.hitPos.y, spawnPosY), 8);"
    in VEH_BIRTH
), "VehBirth must place a driver's origin at the quadblock hit height"
assert (
    "VehBirth_TeleportSelf(d, VEH_BIRTH_SPAWN_RACE_START, 0);" in BOTS
), "the race-start spawn must pass a zero vertical offset"

# The authoring tool records that exact field, unmodified.
author = (ROOT / "ap" / "ap_author.c").read_text(encoding="utf-8")
assert "AP_AuthorNarrow(d->posCurr.y, &clamped)" in author, (
    "the placement row must still be the raw kart position; anything else changes "
    "what the crate lift is measured against"
)

# ── premise 2: the model-unit -> world-unit conversion ───────────────────────

# The vertex fed to the GTE is (frame.pos + vertexByte) << 2 ...
assert "return ((vertexXZ + frameOriginXY) << 2) & 0xfff8ffff;" in QUEUE, (
    "the packed model vertex must still be shifted left by 2"
)
# ... paired so that frame pos.y offsets the vertical vertex byte (vertex->z of
# the (x, y, z) = (byte0, byte1, byte2) triple), and pos.z offsets the depth byte.
assert "u32 vertexXZ = ((u32)vertex->x) | ((u32)vertex->z << 16);" in QUEUE, (
    "the vertical vertex byte must still pack against frame pos.y"
)
assert "return ((u32)(ctx->mf->pos.z + vertex->y)) << 2;" in QUEUE, (
    "the depth vertex byte must still pair with frame pos.z"
)
# ... while the header scale is loaded as (scale >> 2), so the two shifts cancel
# and one model unit is headerScale/0x1000 world units.
assert "scaleXYShift = 0x12 - depthShift;" in QUEUE, "the header XY scale shift must be 0x12"
assert "scaleZShift = 2 - depthShift;" in QUEUE, "the header Z scale shift must be 2"
assert "CTC2(packedScaleXY >> scaleXYShift, 18);" in QUEUE, (
    "the vertical header scale must still be loaded into GTE control register 18"
)

# ── the AP spawn's own instance scale ────────────────────────────────────────

spawn = (ROOT / "ap" / "ap_spawn.c").read_text(encoding="utf-8")
assert "s_spawns[i].scale = 0x1000;" in spawn, (
    "an AP spawn must keep the 1.0 instance scale the conversion assumes"
)

# ── the correction is applied once, in one place ─────────────────────────────

boxes = (ROOT / "ap" / "ap_boxes.c").read_text(encoding="utf-8")
model = (ROOT / "ap" / "ap_box_model.c").read_text(encoding="utf-8")
assert model.count("void AP_BoxModel_SpawnPos(") == 1, "one shared spawn transform, not two"
assert boxes.count("AP_BoxModel_SpawnPos(") == 1, "runtime boxes must go through the shared transform"
assert author.count("AP_BoxModel_SpawnPos(") == 1, "author markers must go through the same transform"

# The collision centre follows the visible centre: both weapon proximity paths
# read the live instance matrix, never the authored row they are lifted off.
for predicate in ("AP_BoxMap_SegmentWithinRadius", "AP_BoxMap_WithinRadius"):
    call = re.search(predicate + r"\(\s*([^,]+),", boxes)
    assert call is not None, f"{predicate} must still be called from ap_boxes.c"
    assert call.group(1).strip() == "inst->matrix.t[0]", (
        f"{predicate} must test the spawned instance's position, not the authored row it is "
        f"lifted off (found {call.group(1).strip()!r})"
    )

print("AP crate anchor premise: PASS")
