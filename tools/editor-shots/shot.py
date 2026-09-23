#!/usr/bin/env python3
"""Headless CTR track screenshots, using the box-placement editor.

Renders one PNG per camera with Mesa llvmpipe through SDL's offscreen video
driver: no display, no GPU, no sudo. See tools/editor-shots/README.md next to
this script for setup and full usage.

Examples
  shot.py --track "Mystery Caves" --box 13                 # driver, side and wide views of AP box 13
  shot.py --track 9 --eye -8363,120,6277 --look -7750,444,6233 --name test
  shot.py --track crash-cove --eye 0,3000,0 --rot -1024,0,0 --marker -3542,599,3423

Coordinates are LEV world units (y up). --marker and --box take the AP
placement anchor (the crate's bottom face); the drawn crate is lifted by 54
units, as the AP client does. Rotation units are 4096 per full turn.

Pictures use the Steam client's video settings (Native render scale, 16:9,
increased draw distance, 1600x900) and draw AP boxes with the AP client's own
box model and face art. --box-art swaps in candidate face art for comparisons.

Paths (all overridable by env var, see README.md):
  CTR_EDITOR_SOURCE     editor source tree (default: this script's repo)
  CTR_EDITOR_ROOT       working dir for binary/cache/output (default: ~/ctr-editor-shots)
  CTR_EDITOR_BINARY     the built editor binary (default: $CTR_EDITOR_ROOT/bin/ctr_native_editor)
  CTR_EDITOR_ASSETS     retail assets dir with ctr-u.bin (default: beside the binary)
  CTR_EDITOR_PLACEMENTS ap_placements_data.h (default: $CTR_EDITOR_SOURCE/ap/ap_placements_data.h)
"""
import argparse
import json
import math
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
SOURCE = Path(os.environ.get("CTR_EDITOR_SOURCE", HERE.parent.parent))
ROOT = Path(os.environ.get("CTR_EDITOR_ROOT", Path.home() / "ctr-editor-shots"))
BINARY = Path(os.environ.get("CTR_EDITOR_BINARY", ROOT / "bin" / "ctr_native_editor"))
PROJECTS = ROOT / "projects"
ASSETS = Path(os.environ.get("CTR_EDITOR_ASSETS", BINARY.parent / "assets"))
PLACEMENTS = Path(os.environ.get("CTR_EDITOR_PLACEMENTS", SOURCE / "ap" / "ap_placements_data.h"))
AP_BOX_LIFT = 54  # tools/test-box-offset.c: AP crate origin sits 54 above its anchor
EYE_HEIGHT = 90   # roughly a kart driver's camera above the road
TRACKS = ("Dingo Canyon", "Dragon Mines", "Blizzard Bluff", "Crash Cove", "Tiger Temple",
          "Papu's Pyramid", "Roo's Tubes", "Hot Air Skyway", "Sewer Speedway", "Mystery Caves",
          "Cortex Castle", "N. Gin Labs", "Polar Pass", "Oxide Station", "Coco Park",
          "Tiny Arena", "Slide Coliseum", "Turbo Track")


def slug(name):
    return name.lower().replace("'", "").replace(".", "").replace(" ", "-")


def track_id(text):
    for i, name in enumerate(TRACKS):
        if str(text).lower() in (str(i), slug(name), name.lower()):
            return i
    sys.exit(f"unknown track {text!r}; use a name or LevelID 0-17")


def triple(text):
    parts = [int(round(float(v))) for v in text.split(",")]
    if len(parts) != 3:
        raise argparse.ArgumentTypeError("expected x,y,z")
    return parts


def project_for(level):
    """Extract the retail LEV/VRM (cached) and return (project json path, project dict)."""
    cmd = [sys.executable, str(SOURCE / "tools" / "run_editor_project.py"), "--retail-track", str(level),
           "--binary", str(BINARY), "--assets", str(ASSETS), "--projects", str(PROJECTS), "--print-command"]
    subprocess.run(cmd, cwd=SOURCE, check=True, stdout=subprocess.DEVNULL)
    path = PROJECTS / f"{slug(TRACKS[level])}-ctr-letters.editor.json"
    return path, json.loads(path.read_text())


def lev_info(lev_path):
    cache = Path(str(lev_path) + ".inspect.json")
    if not cache.exists():
        out = subprocess.run([sys.executable, "-m", "levtool", "inspect", "--json", str(lev_path)],
                             cwd=SOURCE, check=True, capture_output=True, text=True).stdout
        cache.write_text(out)
    return json.loads(cache.read_text())


def ap_box(level, ordinal):
    rows = re.findall(r"\{\s*(-?\d+),\s*(-?\d+),\s*(-?\d+),\s*(-?\d+),\s*(-?\d+)\s*\}",
                      PLACEMENTS.read_text())
    mine = [tuple(map(int, r[1:])) for r in rows if int(r[0]) == level]
    if not 1 <= ordinal <= len(mine):
        sys.exit(f"{TRACKS[level]} has {len(mine)} AP boxes; got {ordinal}")
    return mine[ordinal - 1]  # x, y, z, rot_y


def look_rotation(eye, target):
    dx, dy, dz = (t - e for t, e in zip(target, eye))
    yaw = (2048 + round(math.atan2(dx, dz) * 2048 / math.pi)) & 0xFFF
    pitch = round(math.atan2(dy, math.hypot(dx, dz)) * 2048 / math.pi)
    return [PITCH_SIGN * pitch, yaw, 0]


PITCH_SIGN = 1  # measured 2026-09-23: positive rx tilts the view up, negative looks down


def box_views(info, anchor):
    """Driver's-eye view from the road behind the box, a wide side view, and a top view."""
    x, y, z = anchor
    cps = info["checkpoints"]
    near = min(cps, key=lambda c: (c["position"][0] - x) ** 2 + (c["position"][2] - z) ** 2)
    back = near
    for _ in range(40):  # walk back along the track until ~1200 units away
        prev = cps[back["links"]["backward"]]
        back = prev
        if math.hypot(back["position"][0] - x, back["position"][2] - z) > 1200:
            break
    bx, by, bz = back["position"]
    top = [x, y + AP_BOX_LIFT, z]
    driver_eye = [bx, by + EYE_HEIGHT, bz]
    # side view: perpendicular to the local track direction, 1600 out, a little above the box
    fwd = cps[near["links"]["forward"]]["position"]
    tx, tz = fwd[0] - near["position"][0], fwd[2] - near["position"][2]
    n = math.hypot(tx, tz) or 1
    sx, sz = -tz / n, tx / n
    road_y = near["position"][1]
    mid = [x, (road_y + y) // 2 + 40, z]

    def track_distance(p):  # how far a point is from the driving line (smaller = inside the track space)
        return min(math.hypot(c["position"][0] - p[0], c["position"][2] - p[2]) for c in cps)

    # pick the side of the track that is open space, not solid rock (the camera
    # sees through walls from behind, which makes a confusing picture)
    side = min((1, -1), key=lambda k: track_distance([x + k * sx * 900, 0, z + k * sz * 900]))
    side_eye = [x + side * sx * 900, road_y + 150, z + side * sz * 900]
    wide_eye = [x + side * sx * 1600, y + 350, z + side * sz * 1600]
    return {
        "driver": (driver_eye, look_rotation(driver_eye, top)),
        "side": (side_eye, look_rotation(side_eye, mid)),
        "wide": (wide_eye, look_rotation(wide_eye, mid)),
        "top": ([x, y + 2500, z], [-1024, 0, 0]),
    }


# A common Steam client video config: render_scale 0 (Native: the 3D is
# rendered at window resolution and presented directly, not through the
# 512x216 PSX raster), aspect_ratio 1 (16:9) and increase_draw_distance on.
# --retail-raster reproduces the old 800x600 PSX look.
VIDEO = {"render_scale": 0, "aspect": 1, "draw_distance": 1, "window": "1600x900"}
RETAIL_RASTER = {"render_scale": 1, "aspect": 0, "draw_distance": 0, "window": "800x600"}


def atlas_from_art(face_png, edge_png=None, far_png=None):
    """Build a raw 128x64 RGBA atlas file in the AP client's rect layout
    (tools/apbox-texture/README.md): 16x16 wood at (0,0), 64x64 face at (16,0),
    32x32 far face at (80,0). Missing pieces are filled from the face art."""
    from PIL import Image
    atlas = Image.new("RGBA", (128, 64), (0, 0, 0, 0))
    face = Image.open(face_png).convert("RGBA")
    atlas.paste(face.resize((64, 64), Image.NEAREST), (16, 0))
    edge = Image.open(edge_png).convert("RGBA") if edge_png else face
    atlas.paste(edge.resize((16, 16), Image.NEAREST), (0, 0))
    far = Image.open(far_png).convert("RGBA") if far_png else face
    atlas.paste(far.resize((32, 32), Image.NEAREST), (80, 0))
    fd, path = tempfile.mkstemp(prefix="apbox-atlas-", suffix=".rgba")
    os.write(fd, atlas.tobytes())
    os.close(fd)
    return path


def render(project_path, project, eye, rot, markers, out_png, speed=32, start=60, video=VIDEO, atlas=None, framed=False,
           face=None):
    src = project["sources"]
    with tempfile.TemporaryDirectory(prefix="ctr-shot-") as tmp:
        tmp = Path(tmp)
        sidecar = tmp / "shot.editor.json"
        shutil.copy(project_path, sidecar)  # the editor may autosave; never touch the cached project
        cmd = [str(BINARY),
               "--editor-lev", src["lev"]["path"], "--editor-lev-sha256", src["lev"]["sha256"],
               "--editor-vrm", src["vrm"]["path"], "--editor-vrm-sha256", src["vrm"]["sha256"],
               "--editor-source-lev", src["lev"]["path"], "--editor-source-lev-sha256", src["lev"]["sha256"],
               "--editor-host-slot", str(project["host_slot"]), "--editor-sidecar", str(sidecar),
               "--editor-log", str(tmp / "editor.log"), "--editor-validation-ok",
               "--editor-camera", ",".join(map(str, [*map(int, eye), *rot, speed])),
               "--editor-dump-dir", str(tmp / "frames"), "--editor-dump-count", "1",
               "--editor-dump-start", str(start), "--editor-dump-nohud",
               "--editor-dump-mute", "--editor-dump-skip-intro",
               "--editor-dump-render-scale", str(video["render_scale"]), "--editor-dump-aspect", str(video["aspect"]),
               "--editor-dump-draw-distance", str(video["draw_distance"]), "--editor-dump-window", video["window"]]
        if atlas:
            cmd += ["--editor-apbox-atlas", atlas]
        if framed:
            cmd += ["--editor-apbox-framed"]
        if face:
            cmd += ["--editor-apbox-framed", "--editor-apbox-face", face]
        cmd += os.environ.get("CTR_EDITOR_EXTRA_ARGS", "").split()
        for i, (mx, my, mz, mry) in enumerate(markers, start=1):
            cmd += ["--editor-object", f"{i},ap-candidate,{mx},{my},{mz},0,{mry},0"]
        env = dict(os.environ, SDL_VIDEO_DRIVER="offscreen", SDL_AUDIO_DRIVER="dummy",
                   LIBGL_ALWAYS_SOFTWARE="1")
        proc = subprocess.run(["nice", "-n", "10", "timeout", "240", *cmd], cwd=BINARY.parent, env=env,
                              capture_output=True, text=True)
        frames = sorted((tmp / "frames").glob("*.bmp")) if (tmp / "frames").exists() else []
        if proc.returncode != 0 or not frames:
            sys.stderr.write(proc.stdout[-3000:] + proc.stderr[-3000:])
            sys.exit(f"render failed (exit {proc.returncode})")
        from PIL import Image
        Image.open(frames[0]).convert("RGB").save(out_png)
    print(f"{out_png}  eye={','.join(map(str, map(int, eye)))} rot={','.join(map(str, rot))}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--track", required=True, help="track name, slug or LevelID 0-17")
    ap.add_argument("--box", type=int, help="AP item box number on this track (1-based, as in 'Item Box N')")
    ap.add_argument("--views", default="driver,side,wide", help="views for --box: driver,side,wide,top")
    ap.add_argument("--eye", type=triple, help="camera position x,y,z")
    ap.add_argument("--look", type=triple, help="point to aim the camera at")
    ap.add_argument("--rot", type=triple, help="raw camera rotation pitch,yaw,roll instead of --look")
    ap.add_argument("--marker", type=triple, action="append", default=[],
                    help="extra AP-crate marker at an anchor x,y,z (repeatable)")
    ap.add_argument("--no-lift", action="store_true", help="draw markers at the exact point, not anchor+54")
    ap.add_argument("--out", type=Path, default=ROOT / "out")
    ap.add_argument("--name", help="file name prefix")
    ap.add_argument("--start", type=int, default=60, help="frames to settle before the capture")
    ap.add_argument("--retail-raster", action="store_true",
                    help="old look: 800x600 window, 4:3, PSX raster (render_scale 1), normal draw distance")
    ap.add_argument("--window", help="window size WxH (default 1600x900)")
    ap.add_argument("--box-art", type=Path,
                    help="64x64 face PNG to put on the AP box instead of the art compiled into the client")
    ap.add_argument("--box-framed", action="store_true",
                    help="build the AP box like the retail crate: face square plus a wood border ring (16x16 edge rect)")
    ap.add_argument("--box-face", choices=("jurnth", "wumpa", "plain"),
                    help="framed box with a recoloured crate face from the disc behind the AP logo (implies --box-framed)")
    ap.add_argument("--box-edge", type=Path, help="16x16 edge PNG for the atlas wood rect (not drawn by the client today)")
    # let "--eye -7750,120,6233" work: argparse would read the minus sign as an option
    argv, raw = [], sys.argv[1:]
    for i, a in enumerate(raw):
        if argv and argv[-1] in ("--eye", "--look", "--rot", "--marker") and a.startswith("-"):
            argv[-1] = f"{argv[-1]}={a}"
        else:
            argv.append(a)
    args = ap.parse_args(argv)

    level = track_id(args.track)
    project_path, project = project_for(level)
    lift = 0 if args.no_lift else AP_BOX_LIFT
    markers = [(x, y + lift, z, 0) for x, y, z in args.marker]
    shots = {}
    prefix = args.name or slug(TRACKS[level])
    if args.box:
        bx, by, bz, bry = ap_box(level, args.box)
        markers.append((bx, by + lift, bz, bry))
        prefix = args.name or f"{slug(TRACKS[level])}-box{args.box}"
        views = box_views(lev_info(project["sources"]["lev"]["path"]), (bx, by, bz))
        for v in args.views.split(","):
            shots[v] = views[v]
        print(f"{TRACKS[level]} AP box {args.box}: anchor {bx},{by},{bz} rot_y {bry}")
    if args.eye:
        if args.rot:
            rot = args.rot
        elif args.look:
            rot = look_rotation(args.eye, args.look)
        else:
            sys.exit("--eye needs --look or --rot")
        shots = {"custom": (args.eye, rot)} if not args.box else {**shots, "custom": (args.eye, rot)}
    if not shots:
        sys.exit("give --box and/or --eye")
    video = dict(RETAIL_RASTER if args.retail_raster else VIDEO)
    if args.window:
        video["window"] = args.window
    atlas = atlas_from_art(args.box_art, args.box_edge) if args.box_art else None
    args.out.mkdir(parents=True, exist_ok=True)
    try:
        for view, (eye, rot) in shots.items():
            render(project_path, project, eye, rot, markers, args.out / f"{prefix}-{view}.png", start=args.start,
                   video=video, atlas=atlas, framed=args.box_framed, face=args.box_face)
    finally:
        if atlas:
            os.unlink(atlas)


if __name__ == "__main__":
    main()
