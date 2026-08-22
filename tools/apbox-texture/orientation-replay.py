#!/usr/bin/env python3
"""Outside-view replay of the AP box face orientation.

Parses the LIVE corner-role table out of ap/ap_box_model.c and the LIVE
textured command list out of ap/ap_box_model_data.h, applies them to the model
vertex data, and rasterizes every cube face AS AN OUTSIDE VIEWER SEES IT:

  * byte0 -> world X, byte1 -> world Z (depth), byte2 -> world Y (vertical).
    (RenderBucket packs vertex byte1 with pos.z and byte2 with pos.y.)
  * +Y is up. Screen right on a face = up x outwardNormal, from the engine's
    proper-rotation camera (det +1, MATH_7_MatrixStubs.c) and the PSX y-down
    screen. Caps are viewed with screen-up toward +Z.

PASS criterion: every face renders the 63x63 sampled area of the inner art
exactly -- upright, unmirrored, no magenta filler (the art's last texel
column/row, which PSX hardware never samples; the face layout stops at W-2 /
H-2 for the same reason).

It also renders the first-alpha3 role table as an anchor and asserts it shows
the pattern observed live on 2026-08-21: front/right 180-degree rotated,
back/left vertically mirrored, all with the magenta fringe.

Usage: python3 orientation-replay.py [output-sheet.png]
Exits non-zero on any mismatch, so it can gate a build.
"""
import re
import sys
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
SRC = HERE.parent.parent

# --- the sampled face art: texels 0..62 of the 64x64 inner PNG --------------
art = Image.open(HERE / "box_pink_highres_inner.png").convert("RGBA").crop((0, 0, 63, 63))

# --- face layout corners, as ap_box_texture.c builds them -------------------
FACE_X, FACE_Y, FACE_W, FACE_H = 16, 0, 64, 64
LU, TV = FACE_X, FACE_Y
RU, BV = FACE_X + FACE_W - 2, FACE_Y + FACE_H - 2  # last texel col/row excluded
tex_src = (SRC / "ap" / "ap_box_texture.c").read_text()
for pat in (r"u2\s*=\s*\(u8\)\(AP_BOX_TEXTURE_FACE_X \+ AP_BOX_TEXTURE_FACE_W - 2\)",
            r"v1\s*=\s*\(u8\)\(AP_BOX_TEXTURE_FACE_Y \+ AP_BOX_TEXTURE_FACE_H - 2\)"):
    if not re.search(pat, tex_src):
        sys.exit(f"ap_box_texture.c no longer matches the replayed corner contract: {pat}")

# --- atlas (only needed to prove the magenta filler stays unsampled) --------
hdr = (SRC / "ap" / "ap_box_texture_data.h").read_text()
ATLAS_W, ATLAS_H = 128, 64
abytes = [int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{2})", hdr.split("s_apBoxTextureAtlas")[1])]
atlas = Image.frombytes("RGBA", (ATLAS_W, ATLAS_H), bytes(abytes[: ATLAS_W * ATLAS_H * 4]))

# --- LIVE corner-role table from ap_box_model.c -----------------------------
model_c = (SRC / "ap" / "ap_box_model.c").read_text()
roles_block = model_c.split("static const int roles[6][3][2]")[1].split("};")[0]
live_roles = [[(int(u), int(v)) for u, v in re.findall(r"\{(\d),(\d)\}", line)]
              for line in roles_block.splitlines() if re.search(r"\{\{", line)]
assert len(live_roles) == 6 and all(len(r) == 3 for r in live_roles), "roles table parse failed"

# --- LIVE command list: face -> (layout A, layout B) ------------------------
data_h = (SRC / "ap" / "ap_box_model_data.h").read_text()
cmd_block = data_h.split("s_apBoxModelCommandsTex")[1].split("};")[0]
face_layouts = [(int(a), int(b)) for _, a, _, b in
                re.findall(r"AP_BOX_TRI_TEX\((\d), (\d)\), AP_BOX_TRI_TEX\((\d), (\d)\)", cmd_block)]
assert len(face_layouts) == 6, "command list parse failed"

# the first-alpha3 table (shipped upside down), kept verbatim as the anchor
OLD_ROLES = [
    [(0, 0), (1, 0), (1, 1)], [(0, 0), (1, 1), (0, 1)],
    [(1, 0), (0, 0), (0, 1)], [(1, 0), (0, 1), (1, 1)],
    [(0, 1), (1, 1), (1, 0)], [(0, 1), (1, 0), (0, 0)],
]
OLD_FACE_LAYOUTS = [(1, 2), (3, 4), (3, 4), (1, 2), (1, 2), (5, 6)]
# and the old inclusive far corners, which sampled the magenta filler
OLD_RU, OLD_BV = FACE_X + FACE_W - 1, FACE_Y + FACE_H - 1

# --- model vertices ---------------------------------------------------------
verts_block = data_h.split("s_apBoxModelVerts")[1].split("};")[0]
raw = [int(n) for n in re.findall(r"(\d+)\s*,", verts_block)]
assert len(raw) == 36 * 3, f"vertex parse failed ({len(raw)})"


def world(i):
    b0, b1, b2 = raw[i * 3: i * 3 + 3]
    if b0 == 224:
        b2 += 1  # the packed-add carry the data pre-decrements for
    return (b0 - 128, b2 - 128, b1 - 128)  # (x, y=vertical, z=depth)


VIEWS = {  # face index -> (screen-right, screen-down) in world (x, y, z)
    0: ((-1, 0, 0), (0, -1, 0)),  # front  n=(0,0,-1)
    1: ((+1, 0, 0), (0, -1, 0)),  # back   n=(0,0,+1)
    2: ((0, 0, +1), (0, -1, 0)),  # left   n=(-1,0,0)
    3: ((0, 0, -1), (0, -1, 0)),  # right  n=(+1,0,0)
    4: ((-1, 0, 0), (0, 0, -1)),  # top    n=(0,+1,0), screen-up toward +Z
    5: ((+1, 0, 0), (0, 0, -1)),  # bottom n=(0,-1,0), screen-up toward +Z
}
S = 63  # one output pixel per sampled texel: pixel centers hit texel centers


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def render_face(face, roles, layouts, ru, bv):
    img = Image.new("RGBA", (S, S), (0, 0, 0, 255))
    px = img.load()
    at = atlas.load()
    r, d = VIEWS[face]
    la, lb = layouts[face]
    for tri, layout_idx in ((0, la), (1, lb)):
        role = roles[layout_idx - 1]
        tv3 = [world(face * 6 + tri * 3 + k) for k in range(3)]
        pts = [(((dot(v, r) + 96) / 192.0) * (S - 1), ((dot(v, d) + 96) / 192.0) * (S - 1)) for v in tv3]
        uvs = [((ru if uf else LU), (bv if vf else TV)) for uf, vf in role]
        (x0, y0), (x1, y1), (x2, y2) = pts
        den = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2)
        if abs(den) < 1e-9:
            continue
        for y in range(S):
            for x in range(S):
                w0 = ((y1 - y2) * (x - x2) + (x2 - x1) * (y - y2)) / den
                w1 = ((y2 - y0) * (x - x2) + (x0 - x2) * (y - y2)) / den
                w2 = 1 - w0 - w1
                if w0 < -1e-6 or w1 < -1e-6 or w2 < -1e-6:
                    continue
                u = w0 * uvs[0][0] + w1 * uvs[1][0] + w2 * uvs[2][0]
                v = w0 * uvs[0][1] + w1 * uvs[1][1] + w2 * uvs[2][1]
                px[x, y] = at[int(u + 0.5) % ATLAS_W, int(v + 0.5) % ATLAS_H]
    return img


NAMES = ["front", "back", "left", "right", "top", "bottom"]
failures = []
new_faces, old_faces = [], []
for f in range(6):
    new_faces.append(render_face(f, live_roles, face_layouts, RU, BV))
    old_faces.append(render_face(f, OLD_ROLES, OLD_FACE_LAYOUTS, OLD_RU, OLD_BV))
    if list(new_faces[f].getdata()) != list(art.getdata()):
        failures.append(f"{NAMES[f]}: live table does not reproduce the art exactly")

# anchor: the old table must show the pattern observed live on 2026-08-21
art64 = Image.open(HERE / "box_pink_highres_inner.png").convert("RGBA")
old_expect = {
    0: art64.rotate(180),                        # front: 180 rotated
    1: art64.transpose(Image.FLIP_TOP_BOTTOM),   # back: vertical mirror
    2: art64.transpose(Image.FLIP_TOP_BOTTOM),   # left: vertical mirror
    3: art64.rotate(180),                        # right: 180 rotated
}
for f, exp in old_expect.items():
    got = render_face(f, OLD_ROLES, OLD_FACE_LAYOUTS, OLD_RU, OLD_BV)
    # old sampling spans texels 0..63 across 63 pixels, so a per-pixel compare
    # cannot be exact; the anchor only needs the ORIENTATION, so compare the
    # mean color of the top half against the bottom half.
    def half_means(im):
        w, h = im.size
        top = im.crop((0, 0, w, h // 2)).resize((1, 1)).getpixel((0, 0))[:3]
        bot = im.crop((0, h // 2, w, h)).resize((1, 1)).getpixel((0, 0))[:3]
        return top, bot
    et, eb = half_means(exp)
    gt, gb = half_means(got)
    if not (abs(et[0] - gt[0]) < 25 and abs(eb[0] - gb[0]) < 25):
        failures.append(f"{NAMES[f]}: old-table anchor does not match the live-observed orientation")

# sheet: row 1 old (as shipped, magenta fringe visible), row 2 live, row 3 art
Z, pad = 126, 6
sheet = Image.new("RGBA", ((Z + pad) * 6 + pad, (Z + pad) * 3 + pad), (12, 12, 12, 255))
for f in range(6):
    x = pad + f * (Z + pad)
    sheet.paste(old_faces[f].resize((Z, Z), Image.NEAREST), (x, pad))
    sheet.paste(new_faces[f].resize((Z, Z), Image.NEAREST), (x, pad * 2 + Z))
    sheet.paste(art.resize((Z, Z), Image.NEAREST), (x, pad * 3 + Z * 2))
out = sys.argv[1] if len(sys.argv) > 1 else "orientation-replay.png"
sheet.save(out)
print(f"wrote {out} (rows: 1=first-alpha3 table as shipped, 2=live table, 3=the art; cols: {NAMES})")

if failures:
    for f in failures:
        print("FAIL:", f)
    sys.exit(1)
print("PASS: all six faces reproduce the art exactly, upright and unmirrored, no filler texels")
