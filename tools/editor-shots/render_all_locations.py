#!/usr/bin/env python3
"""AP item box location pictures: one "findable" labelled picture per AP box.

For every AP item box on the 18 box tracks this renders a chase-style view
from behind the box along the direction of travel (further back and higher
than shot.py's `driver` preset), measures how much of the box is visible,
labels it, and writes a contact sheet per track plus a manifest CSV. Used
for the player-facing reference guide of where every "<Track>: Item Box N"
location is.

Stored cameras (ap-box-locations/cameras.json next to this script)
  Every final camera (eye and look point, per box) is stored in the cameras
  file together with the render and label settings (window, video options,
  box art, caption format, thresholds). A plain run re-renders every picture,
  contact sheet and caption from that file without searching, so the whole
  set can be regenerated when the box art changes. Hand-corrected cameras
  are written into the same file with source "hand" and are never replaced
  by a later --search unless --replace-hand is given.

Camera search (--search)
  The travel direction comes from the track's checkpoint path (levtool
  inspect). Candidates are tried in order (see CANDIDATES): behind the box
  along the path at several distances and heights, straight back from the
  box, from ahead looking back, from the open side, and from high above.
  Each is rendered with and without the target box; the pixel difference is
  the box's visible area. The first candidate reaching min_visible pixels is
  kept, otherwise the one with the most visible pixels (and it is flagged).

Labels
  Every AP box of the track is drawn, as players see them. The target box
  gets a ring, an arrow and its number; other AP boxes visible in the frame
  get a small number tag; a caption strip carries the player-facing location
  name "<Track>: Item Box N" (the apworld's name). Box N is the Nth row for
  that level in ap_placements_data.h, counted in file order, which is how
  both the client and the apworld number slots.

Examples
  render_all_locations.py                                   # re-render everything from the stored cameras
  render_all_locations.py --track "Mystery Caves"           # one track
  render_all_locations.py --track mystery-caves --box 13    # one box
  render_all_locations.py --box-face plain --out /tmp/x     # same cameras, other box art (not stored)
  render_all_locations.py --search                          # (re)run the camera search, keep hand cameras
  render_all_locations.py --track 9 --box 13 --search --candidate side     # try one candidate
  render_all_locations.py --track 9 --box 13 --eye -8500,600,5000 --look -7750,444,6233   # hand camera
  render_all_locations.py --track polar-pass --box 12 --start 140   # later capture frame for one box (kart moved on)

A run with --box updates that box's rows in the output manifest and rebuilds
the track's contact sheet from the pictures on disk.
"""
import argparse
import concurrent.futures as cf
import csv
import json
import math
import os
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, str(Path(__file__).resolve().parent))
import shot  # noqa: E402

W, H = 1280, 720
FOCAL = 495.0          # pixels at 1280x720 (Native scale, 16:9); measured 2026-09-23 against marker offsets
BOX_HALF = 55          # half the drawn crate's size in world units, roughly
DIFF_LEVEL = 24        # summed RGB difference that counts as a changed pixel
MIN_VISIBLE = 400      # default visible-pixel threshold for "pass"
LOW_VISIBLE = 120      # below this the box is effectively not findable
PINK = (255, 64, 190)
YELLOW = (255, 235, 0)
FONT_PATHS = ("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
              "/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf")

# name, kind, distance, height. "path": walk back (negative: ahead) along the
# checkpoint path; "straight": straight back from the box against the travel
# direction; "side": out to the open side; heights are above the path/box.
CANDIDATES = (
    ("behind", "path", 1600, 400),
    ("behind-near", "path", 1100, 260),
    ("straight-back", "straight", 1100, 260),
    ("behind-close", "path", 700, 160),
    ("behind-high", "path", 1600, 1000),
    ("ahead", "path", -1100, 260),
    ("side", "side", 900, 300),
    ("above", "path", 400, 1500),
)


def font(size):
    for p in FONT_PATHS:
        if os.path.exists(p):
            return ImageFont.truetype(p, size)
    return ImageFont.load_default(size=size)


def track_boxes(level):
    """All AP boxes of a level as (number, (x, y, z, rot_y)), in slot order."""
    out, n = [], 1
    while True:
        try:
            out.append((n, shot.ap_box(level, n)))
        except SystemExit:
            return out
        n += 1


class Path3:
    """The checkpoint path as a closed polyline, in the direction of travel."""

    def __init__(self, cps):
        self.cps = cps
        self.segs = []  # (index a, index b)
        for c in cps:
            f = c["links"]["forward"]
            if 0 <= f < len(cps):
                self.segs.append((c["index"], f))

    def nearest(self, p):
        """Nearest point on the path to p: (segment, t). Height counts double so
        stacked roads pick the right level."""
        best = None
        for a, b in self.segs:
            pa, pb = np.array(self.cps[a]["position"], float), np.array(self.cps[b]["position"], float)
            d = pb - pa
            t = float(np.clip(np.dot(p - pa, d) / max(np.dot(d, d), 1), 0, 1))
            q = pa + t * d
            w = (p - q) * np.array([1, 2, 1])
            dist = float(np.dot(w, w))
            if best is None or dist < best[0]:
                best = (dist, (a, b), t)
        return best[1], best[2]

    def pos(self, i):
        return np.array(self.cps[i]["position"], float)

    def walk(self, seg, t, dist):
        """Point `dist` units along the path from (seg, t); negative = backwards."""
        a, b = seg
        if dist < 0:
            left = -dist
            here = self.pos(a) + t * (self.pos(b) - self.pos(a))
            remaining = np.linalg.norm(here - self.pos(a))
            while left > remaining:
                left -= remaining
                here = self.pos(a)
                prev = self.cps[a]["links"]["backward"]
                if not 0 <= prev < len(self.cps):
                    return here
                b, a = a, prev
                remaining = np.linalg.norm(self.pos(b) - self.pos(a))
            d = self.pos(a) - here
            n = np.linalg.norm(d) or 1
            return here + d / n * left
        left = dist
        here = self.pos(a) + t * (self.pos(b) - self.pos(a))
        remaining = np.linalg.norm(self.pos(b) - here)
        while left > remaining:
            left -= remaining
            here = self.pos(b)
            nxt = self.cps[b]["links"]["forward"]
            if not 0 <= nxt < len(self.cps):
                return here
            a, b = b, nxt
            remaining = np.linalg.norm(self.pos(b) - self.pos(a))
        d = self.pos(b) - here
        n = np.linalg.norm(d) or 1
        return here + d / n * left

    def direction(self, seg, t):
        """Horizontal travel direction around (seg, t), smoothed over ~600 units."""
        ahead, behind = self.walk(seg, t, 300), self.walk(seg, t, -300)
        d = ahead - behind
        d[1] = 0
        n = np.linalg.norm(d)
        return d / n if n else np.array([0.0, 0.0, 1.0])


def camera_for(cand, path, box):
    name, kind, dist, height = cand
    centre = np.array([box[0], box[1] + shot.AP_BOX_LIFT, box[2]], float)
    seg, t = path.nearest(centre)
    fwd = path.direction(seg, t)
    if kind == "path":
        ground = path.walk(seg, t, -dist)
        eye = ground + np.array([0, height, 0])
        # a box well above the road (ramps, jumps): keep the camera at least a bit above it
        eye[1] = max(eye[1], centre[1] + height * 0.35)
    elif kind == "straight":
        eye = centre - fwd * dist + np.array([0, height, 0])
    else:  # side: the open side, as shot.box_views does
        side_vec = np.array([-fwd[2], 0, fwd[0]])

        def track_distance(q):
            return min(math.hypot(c["position"][0] - q[0], c["position"][2] - q[2]) for c in path.cps)
        k = min((1, -1), key=lambda s: track_distance(centre + s * side_vec * dist))
        eye = centre + k * side_vec * dist + np.array([0, height, 0])
    return [int(round(v)) for v in eye], [int(round(v)) for v in centre]


def project(eye, look, p):
    """World point -> (x, y, depth) in the 1280x720 picture, or None if behind."""
    eye, look, p = (np.array(v, float) for v in (eye, look, p))
    f = look - eye
    f /= np.linalg.norm(f)
    r = np.cross(f, [0, 1, 0])
    r /= np.linalg.norm(r) or 1
    u = np.cross(r, f)
    d = p - eye
    z = float(d @ f)
    if z <= 1:
        return None
    return W / 2 + FOCAL * float(d @ r) / z, H / 2 - FOCAL * float(d @ u) / z, z


def render_arr(ctx, eye, look, markers, start=None):
    fd, png = tempfile.mkstemp(prefix="apref-", suffix=".png")
    os.close(fd)
    try:
        rot = shot.look_rotation(eye, look)
        try:
            shot.render(ctx["project_path"], ctx["project"], eye, rot, markers, png, video=ctx["video"],
                        face=ctx["face"], start=ctx["start"] if start is None else start)
        except SystemExit as e:
            raise RuntimeError(f"render failed: {e}")
        return np.asarray(Image.open(png).convert("RGB"))
    finally:
        os.unlink(png)


def diff_mask(a, b):
    return np.abs(a.astype(np.int16) - b.astype(np.int16)).sum(axis=2) > DIFF_LEVEL


def target_mask(a, b, eye, look, box):
    """Pixels that change when the target box is removed, kept only in a
    window around where the box projects. Other things in the scene (karts)
    can move between two renders; the window keeps them out of the count."""
    mask = diff_mask(a, b)
    pr = project(eye, look, (box[0], box[1] + shot.AP_BOX_LIFT, box[2]))
    keep = np.zeros_like(mask)
    if pr:
        rad = int(max(30, 2.2 * FOCAL * BOX_HALF / pr[2]))
        x0, x1 = max(0, int(pr[0]) - rad), min(W, int(pr[0]) + rad)
        y0, y1 = max(0, int(pr[1]) - rad), min(H, int(pr[1]) + rad)
        if x1 > x0 and y1 > y0:
            keep[y0:y1, x0:x1] = True
    return mask & keep


def markers_for(boxes, skip=None):
    lift = shot.AP_BOX_LIFT
    return [(x, y + lift, z, r) for n, (x, y, z, r) in boxes if n != skip]


def search_camera(ctx, number, force=None):
    """Try CANDIDATES in order; return the stored-camera record for the best one."""
    box = dict(ctx["boxes"])[number]
    tried, best = [], None
    for cand in [c for c in CANDIDATES if force is None or c[0] == force]:
        eye, look = camera_for(cand, ctx["path"], box)
        vis = int(target_mask(render_arr(ctx, eye, look, markers_for(ctx["boxes"])),
                              render_arr(ctx, eye, look, markers_for(ctx["boxes"], number)), eye, look, box).sum())
        tried.append(f"{cand[0]}:{vis}")
        if best is None or vis > best[3]:
            best = (cand[0], eye, look, vis)
        if vis >= ctx["min_visible"]:
            break
    return dict(camera=best[0], source="auto-search", eye=best[1], look=best[2], anchor=list(box[:3]),
                cameras_tried=" ".join(tried))


def status_for(ctx, visible):
    if visible >= ctx["min_visible"]:
        return "pass"
    return "flag-small" if visible >= ctx["low_visible"] else "flag-hidden"


def render_box(ctx, number, eye, look, start=None):
    """Render box `number` from a fixed camera: the picture, its visible-pixel
    mask, and the other AP boxes visible in the frame. `start` overrides the
    settle frames (a box whose stored camera catches an AI kart on it)."""
    boxes = ctx["boxes"]
    box = dict(boxes)[number]
    lift = shot.AP_BOX_LIFT
    img = render_arr(ctx, eye, look, markers_for(boxes), start)
    without = render_arr(ctx, eye, look, markers_for(boxes, number), start)
    mask = target_mask(img, without, eye, look, box)
    info = dict(img=img, mask=mask, visible=int(mask.sum()), others=[],
                target_proj=project(eye, look, (box[0], box[1] + lift, box[2])))
    info["status"] = status_for(ctx, info["visible"])
    # other AP boxes in view: diff the "others only" render against no boxes
    in_view = []
    for n, (x, y, z, r) in boxes:
        if n == number:
            continue
        pr = project(eye, look, (x, y + lift, z))
        if pr and -40 < pr[0] < W + 40 and -40 < pr[1] < H + 40:
            in_view.append((n, pr))
    if in_view:
        omask = diff_mask(without, render_arr(ctx, eye, look, [], start))
        for n, (px, py, depth) in in_view:
            rad = max(12, int(FOCAL * BOX_HALF * 1.6 / depth))
            x0, x1 = max(0, int(px) - rad), min(W, int(px) + rad)
            y0, y1 = max(0, int(py) - rad), min(H, int(py) + rad)
            if x1 <= x0 or y1 <= y0:
                continue
            sub = omask[y0:y1, x0:x1]
            if sub.sum() >= 15:
                ys, xs = np.nonzero(sub)
                info["others"].append((n, (x0 + xs.mean(), y0 + ys.min()), depth))
    return info


def annotate(ctx, number, shot_info):
    img = Image.fromarray(shot_info["img"]).convert("RGB")
    draw = ImageDraw.Draw(img, "RGBA")
    mask = shot_info["mask"]
    # other boxes first, so the target's marks sit on top
    f_small = font(22)
    for n, (cx, top), _depth in shot_info["others"]:
        text = str(n)
        tw = draw.textlength(text, font=f_small)
        bx0, by1 = cx - tw / 2 - 7, max(30, top - 6)
        draw.rounded_rectangle([bx0, by1 - 30, bx0 + tw + 14, by1], radius=6, fill=(20, 20, 20, 200),
                               outline=(255, 255, 255, 230), width=2)
        draw.text((bx0 + 7, by1 - 29), text, font=f_small, fill=(255, 255, 255))
    if mask.any():
        ys, xs = np.nonzero(mask)
        cx, cy = (xs.min() + xs.max()) / 2, (ys.min() + ys.max()) / 2
        rad = max(xs.max() - xs.min(), ys.max() - ys.min()) / 2 * 1.35 + 14
    elif shot_info["target_proj"]:
        cx, cy, depth = shot_info["target_proj"]
        rad = FOCAL * BOX_HALF / depth * 1.35 + 14
    else:
        cx, cy, rad = W / 2, H / 2, 40
    rad = max(rad, 28)
    for w, col in ((9, (0, 0, 0, 170)), (5, YELLOW + (255,))):
        draw.ellipse([cx - rad, cy - rad, cx + rad, cy + rad], outline=col, width=w)
    # number badge with an arrow to the ring; above the box unless too close to the top
    f_big = font(44)
    text = str(number)
    tw = draw.textlength(text, font=f_big)
    bw, bh = tw + 30, 58
    gap = 70
    above = cy - rad - gap - bh > 10
    bx = min(max(cx - bw / 2, 10), W - bw - 10)
    by = cy - rad - gap - bh if above else min(cy + rad + gap, H - 60 - bh)
    ax, ay = bx + bw / 2, (by + bh) if above else by
    ang = math.atan2(cy - ay, cx - ax)
    tip = (cx - math.cos(ang) * (rad + 4), cy - math.sin(ang) * (rad + 4))
    for w, col in ((9, (0, 0, 0, 170)), (5, YELLOW + (255,))):
        draw.line([ax, ay, tip[0], tip[1]], fill=col, width=w)
    head = [tip, (tip[0] - 22 * math.cos(ang - 0.45), tip[1] - 22 * math.sin(ang - 0.45)),
            (tip[0] - 22 * math.cos(ang + 0.45), tip[1] - 22 * math.sin(ang + 0.45))]
    draw.polygon(head, fill=YELLOW + (255,), outline=(0, 0, 0, 200))
    draw.rounded_rectangle([bx, by, bx + bw, by + bh], radius=10, fill=PINK + (255,), outline=(255, 255, 255, 255),
                           width=3)
    draw.text((bx + 15, by + 4), text, font=f_big, fill=(255, 255, 255))
    # caption strip
    draw.rectangle([0, H - 48, W, H], fill=(0, 0, 0, 200))
    draw.text((16, H - 42), ctx["location"](number), font=font(30), fill=(255, 255, 255))
    note = "yellow ring and arrow: this box" + ("   small tags: other AP boxes" if shot_info["others"] else "")
    f_note = font(16)
    draw.text((W - 16 - draw.textlength(note, font=f_note), H - 32), note, font=f_note, fill=(200, 200, 200))
    return img


def _label_palette():
    """Colours reserved in every saved palette, so the labels never change
    colour. A plain median cut of a picture with little yellow in the scene
    has no bucket near the ring's yellow and turned it pale pink or orange.
    Besides the pure label colours this holds the blends that appear at
    anti-aliased edges (white text on pink, yellow with its black outline)
    and a grey ramp for the caption strip and tags."""
    cols = [YELLOW, PINK, (255, 255, 255), (0, 0, 0), (20, 20, 20), (200, 200, 200)]
    mix = lambda a, b, t: tuple(round(x + (y - x) * t) for x, y in zip(a, b))  # noqa: E731
    for t in (0.25, 0.5, 0.75):
        cols += [mix(PINK, (255, 255, 255), t), mix(YELLOW, (0, 0, 0), t), mix(PINK, (0, 0, 0), t)]
    cols += [(g, g, g) for g in range(40, 256, 24)]
    out = []
    for c in cols:
        if c not in out:
            out.append(c)
    return out


LABEL_COLOURS = _label_palette()


def save_png(img, path, label_mask=None, reserve_in_scene=False):
    """256-colour PNG (about a third of the size of a full-colour one) with
    exact label colours. The label colours are reserved at the end of every
    palette.

    The scene gets a median-cut, dithered palette of 256 minus the reserved
    colours (reserve_in_scene: the whole picture is dithered to the scene
    palette plus the reserved colours, for contact sheets whose shrunken
    pictures carry label colours). Then the pixels under the labels
    (label_mask, the pixels the labels changed) are mapped to their nearest
    colour in the full palette without dithering, so a pure label colour
    stays exactly that colour."""
    img = img.convert("RGB")
    scene_n = 256 - len(LABEL_COLOURS)
    labels = [v for c in LABEL_COLOURS for v in c]
    dither = Image.Dither.NONE if reserve_in_scene else Image.Dither.FLOYDSTEINBERG
    scene = img.quantize(scene_n, method=Image.Quantize.MEDIANCUT, dither=dither)
    flat = (scene.getpalette()[:3 * scene_n] + [0] * (3 * scene_n))[:3 * scene_n] + labels
    if reserve_in_scene:
        pal_img = Image.new("P", (1, 1))
        pal_img.putpalette(flat)
        scene = img.quantize(palette=pal_img, dither=Image.Dither.FLOYDSTEINBERG)
    idx = np.asarray(scene).copy()
    if label_mask is not None and label_mask.any():
        pal = np.array(flat, np.int32).reshape(-1, 3)
        px = np.asarray(img, np.int32)[label_mask]
        near = np.empty(len(px), np.uint8)
        for i in range(0, len(px), 20000):
            d = ((px[i:i + 20000, None, :] - pal[None, :, :]) ** 2).sum(axis=2)
            near[i:i + 20000] = d.argmin(axis=1)
        idx[label_mask] = near
    out = Image.fromarray(idx, "P")
    out.putpalette(flat)
    out.save(path, optimize=True)


def contact_sheet(track_name, tslug, outdir, numbers):
    cols = 4
    tw, th = 320, 180
    rows = math.ceil(len(numbers) / cols)
    head = 56
    sheet = Image.new("RGB", (cols * tw, head + rows * (th + 4)), (24, 24, 24))
    for i, n in enumerate(numbers):
        p = outdir / f"{tslug}-box-{n:02d}.png"
        if p.exists():
            sheet.paste(Image.open(p).convert("RGB").resize((tw, th), Image.LANCZOS),
                        ((i % cols) * tw, head + (i // cols) * (th + 4)))
    base = np.asarray(sheet).copy()
    d = ImageDraw.Draw(sheet)
    d.text((14, 10), f"{track_name}: AP item boxes 1 to {max(numbers)}", font=font(30), fill=(255, 255, 255))
    f = font(30)
    for i, n in enumerate(numbers):
        x, y = (i % cols) * tw, head + (i // cols) * (th + 4)
        tag = str(n)
        wtag = d.textlength(tag, font=f) + 18
        d.rectangle([x, y, x + wtag, y + 40], fill=PINK)
        d.text((x + 9, y + 3), tag, font=f, fill=(255, 255, 255))
    save_png(sheet, outdir / f"{tslug}-overview.png", label_mask=np.any(np.asarray(sheet) != base, axis=2),
             reserve_in_scene=True)


FIELDS = ["track", "box", "location_name", "level_id", "anchor_x", "anchor_y", "anchor_z", "camera", "camera_source",
          "eye_x", "eye_y", "eye_z", "look_x", "look_y", "look_z", "visible_pixels", "auto_check", "cameras_tried",
          "other_boxes_labelled", "file"]

CAMERAS = Path(__file__).resolve().parent / "ap-box-locations" / "cameras.json"


def default_settings():
    """Render and label settings stored in the cameras file, so a re-render
    reproduces the same pictures."""
    return dict(window=f"{W}x{H}", render_scale=shot.VIDEO["render_scale"], aspect=shot.VIDEO["aspect"],
                draw_distance=shot.VIDEO["draw_distance"], start_frames=60, box_face="wumpa", box_tint=None,
                focal=FOCAL, diff_level=DIFF_LEVEL, min_visible=MIN_VISIBLE, low_visible=LOW_VISIBLE,
                caption="{track}: Item Box {box}", file_name="{slug}-box-{box:02d}.png",
                png="256-colour palette (median cut, Floyd-Steinberg, label colours reserved)")


def load_cameras(path):
    if path.exists():
        data = json.loads(path.read_text())
    else:
        data = {"format": "ctr-ap-box-location-cameras", "version": 1, "settings": default_settings(), "tracks": {}}
    for k, v in default_settings().items():
        data["settings"].setdefault(k, v)
    return data


def save_cameras(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    tracks = {t: {str(n): data["tracks"][t][str(n)] for n in sorted(map(int, data["tracks"][t]))}
              for t in sorted(data["tracks"], key=lambda t: shot.track_id(t))}
    data["tracks"] = tracks
    # one line per box keeps diffs of hand corrections readable
    head = {k: v for k, v in data.items() if k != "tracks"}
    lines = [json.dumps(head, indent=1)[:-2] + ",", ' "tracks": {']
    for ti, (t, boxes) in enumerate(tracks.items()):
        lines.append(f"  {json.dumps(t)}: {{")
        items = list(boxes.items())
        for bi, (n, rec) in enumerate(items):
            lines.append(f"   {json.dumps(n)}: {json.dumps(rec)}" + ("," if bi < len(items) - 1 else ""))
        lines.append("  }" + ("," if ti < len(tracks) - 1 else ""))
    lines += [" }", "}"]
    text = "\n".join(lines) + "\n"
    json.loads(text)
    path.write_text(text)


def run_track(level, args, cams):
    name = shot.TRACKS[level]
    tslug = shot.slug(name)
    st = cams["settings"]
    if (st["window"], st["focal"]) != (f"{W}x{H}", FOCAL):
        sys.exit("cameras file window/focal differ from this script's; re-measure FOCAL before changing them")
    project_path, project = shot.project_for(level)
    info = shot.lev_info(project["sources"]["lev"]["path"])
    video = dict(render_scale=st["render_scale"], aspect=st["aspect"], draw_distance=st["draw_distance"],
                 window=st["window"])
    face = args.box_face if args.box_face is not None else st["box_face"]
    if st["box_tint"] and "--editor-apbox-tint" not in os.environ.get("CTR_EDITOR_EXTRA_ARGS", ""):
        os.environ["CTR_EDITOR_EXTRA_ARGS"] = (os.environ.get("CTR_EDITOR_EXTRA_ARGS", "")
                                               + f" --editor-apbox-tint {st['box_tint']}").strip()
    ctx = dict(project_path=project_path, project=project, path=Path3(info["checkpoints"]), boxes=track_boxes(level),
               video=video, face=None if face == "none" else face, min_visible=st["min_visible"],
               low_visible=st["low_visible"], start=st["start_frames"],
               location=lambda n: st["caption"].format(track=name, box=n))
    outdir = args.out / tslug
    outdir.mkdir(parents=True, exist_ok=True)
    numbers = [n for n, _ in ctx["boxes"]]
    todo = [args.box] if args.box else numbers
    stored = cams["tracks"].setdefault(name, {})
    rows = {}

    def one(n):
        box = dict(ctx["boxes"])[n]
        rec = stored.get(str(n))
        if args.eye:
            rec = dict(camera="hand", source="hand", eye=args.eye, look=args.look, anchor=list(box[:3]),
                       cameras_tried="")
            if args.start is not None:
                rec["start"] = args.start
        elif rec is None or (args.search and (rec["source"] != "hand" or args.replace_hand)):
            if not args.search:
                raise SystemExit(f"{name} box {n} has no stored camera; run with --search")
            rec = search_camera(ctx, n, force=args.candidate)
        if rec["anchor"] != list(box[:3]):
            print(f"WARNING {name} box {n}: placement moved since this camera was stored "
                  f"({rec['anchor']} -> {list(box[:3])}); re-run with --search", flush=True)
        if args.start is not None and args.box:
            rec["start"] = args.start
        s = render_box(ctx, n, rec["eye"], rec["look"], rec.get("start"))
        if args.search_failing and s["status"] != "pass" and rec["source"] != "hand" and not args.eye:
            print(f"{name} box {n}: stored camera measures {s['visible']}, searching again", flush=True)
            rec = search_camera(ctx, n, force=args.candidate)
            s = render_box(ctx, n, rec["eye"], rec["look"])
        rec.update(visible_pixels=s["visible"], auto_check=s["status"],
                   other_boxes=[o[0] for o in sorted(s["others"])])
        fn = st["file_name"].format(slug=tslug, box=n)
        labelled = annotate(ctx, n, s)
        save_png(labelled, outdir / fn, label_mask=np.any(np.asarray(labelled) != s["img"], axis=2))
        print(f"{name} box {n}: {rec['camera']} ({rec['source']}) visible={s['visible']} {s['status']}"
              f" [{rec['cameras_tried']}]", flush=True)
        return n, rec, dict(track=name, box=n, location_name=ctx["location"](n), level_id=level, anchor_x=box[0],
                            anchor_y=box[1], anchor_z=box[2], camera=rec["camera"], camera_source=rec["source"],
                            eye_x=rec["eye"][0], eye_y=rec["eye"][1], eye_z=rec["eye"][2], look_x=rec["look"][0],
                            look_y=rec["look"][1], look_z=rec["look"][2], visible_pixels=s["visible"],
                            auto_check=s["status"], cameras_tried=rec["cameras_tried"],
                            other_boxes_labelled=" ".join(map(str, rec["other_boxes"])), file=f"{tslug}/{fn}")

    with cf.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for n, rec, row in pool.map(one, todo):
            stored[str(n)] = rec
            rows[n] = row
    contact_sheet(name, tslug, outdir, numbers)
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", type=Path, default=shot.ROOT / "out" / "ap-box-locations",
                    help="output folder, one subfolder per track (default $CTR_EDITOR_ROOT/out/ap-box-locations)")
    ap.add_argument("--track", action="append", help="track name, slug or LevelID (repeatable; default all 18)")
    ap.add_argument("--box", type=int, help="only this box number (needs exactly one --track)")
    ap.add_argument("--cameras", type=Path, default=CAMERAS, help="stored cameras file (default: the one in the repo)")
    ap.add_argument("--search", action="store_true",
                    help="run the camera search and store the result (hand-set cameras are kept unless --replace-hand)")
    ap.add_argument("--search-failing", action="store_true",
                    help="render from stored cameras, and search again only for boxes that fail the check")
    ap.add_argument("--replace-hand", action="store_true", help="with --search, also replace hand-set cameras")
    ap.add_argument("--candidate", choices=[c[0] for c in CANDIDATES], help="with --search, try only this candidate")
    ap.add_argument("--eye", type=shot.triple, help="hand-set camera position x,y,z for --box (stored as source=hand)")
    ap.add_argument("--look", type=shot.triple, help="point the hand-set camera aims at, x,y,z")
    ap.add_argument("--start", type=int,
                    help="with --box: settle frames before the capture for this box only, stored with its camera "
                         "(default: the stored start_frames setting)")
    ap.add_argument("--jobs", type=int, default=3, help="parallel boxes (default 3)")
    ap.add_argument("--box-face", choices=("wumpa", "jurnth", "plain", "none"),
                    help="override the stored AP box art for this run (none = plain AP cube); not written back")
    argv, raw = [], sys.argv[1:]
    for a in raw:  # let "--eye -7750,120,6233" work
        if argv and argv[-1] in ("--eye", "--look") and a.startswith("-"):
            argv[-1] = f"{argv[-1]}={a}"
        else:
            argv.append(a)
    args = ap.parse_args(argv)
    levels = [shot.track_id(t) for t in args.track] if args.track else list(range(len(shot.TRACKS)))
    if (args.box or args.eye) and len(levels) != 1:
        sys.exit("--box and --eye need exactly one --track")
    if args.start is not None and not args.box:
        sys.exit("--start needs --box")
    if args.eye and not (args.box and args.look):
        sys.exit("--eye needs --box and --look")
    cams = load_cameras(args.cameras)
    args.out.mkdir(parents=True, exist_ok=True)
    manifest = args.out / "manifest.csv"
    existing = {}
    if manifest.exists():
        with manifest.open(newline="") as fh:
            for row in csv.DictReader(fh):
                existing[(int(row["level_id"]), int(row["box"]))] = row
    for level in levels:
        for n, row in run_track(level, args, cams).items():
            existing[(level, n)] = row
        if args.search or args.search_failing or args.eye or args.cameras.exists():
            save_cameras(args.cameras, cams)  # after every track, so an interrupted batch keeps its work
        with manifest.open("w", newline="") as fh:
            wr = csv.DictWriter(fh, fieldnames=FIELDS)
            wr.writeheader()
            for key in sorted(existing):
                wr.writerow({k: existing[key].get(k, "") for k in FIELDS})
    print(f"manifest: {manifest} ({len(existing)} rows); cameras: {args.cameras}")


if __name__ == "__main__":
    main()
