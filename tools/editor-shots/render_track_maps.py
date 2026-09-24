#!/usr/bin/env python3
"""Top-down SVG track maps with numbered AP item box markers.

Draws one small SVG per track from the retail LEV geometry, projected onto the
x/z plane (y is up in LEV world units):

- the road: quadblocks that belong to the checkpoint path (checkpoint_index is
  set) or carry the GROUND flag, facing upwards; shaded by height (smoothed
  along the checkpoints, so humps do not stripe it), lighter is higher, and
  higher road is drawn on top of lower road with a dark edge where it crosses
  over;
- the ground right beside the road (other upward-facing collision floors) as a
  faint backdrop, so ledges next to the road have some context;
- the checkpoint path as a thin centre line with arrows in the driving
  direction, and a chequered start line at checkpoint 0;
- one pink numbered marker per AP box at its anchor, each a link to
  "#box-N". Markers that would overlap are pushed apart and keep a thin leader
  line to their true spot. Shortcut Knowledge boxes get a small orange "M"
  (Medium or Hard) or "H" (Hard) badge. With --rings, a box high above the
  track surface under it, or with no surface under it, gets a white dashed
  ring (a rough guide, off by default; see classify_box).

Screen orientation matches the editor's straight-down camera (yaw 0): +x is to
the right and +z is down, so the map is not mirrored.

Needs shapely (often missing from a system Python). One-time setup:
  python3 -m venv ~/ctr-editor-shots/.venv-maps
  ~/ctr-editor-shots/.venv-maps/bin/pip install shapely
Run:
  ~/ctr-editor-shots/.venv-maps/bin/python render_track_maps.py --track papus-pyramid --out DIR
  ~/ctr-editor-shots/.venv-maps/bin/python render_track_maps.py --track all --out DIR

Output: DIR/<slug>/<slug>-map.svg. Uses shot.py beside this file for the
track list, the cached LEV extraction and the AP placement table.
"""
import argparse
import math
import sys
from pathlib import Path

from shapely.geometry import LineString, Point, Polygon
from shapely.ops import unary_union
from shapely.strtree import STRtree

sys.path.insert(0, str(Path(__file__).resolve().parent))
import shot  # noqa: E402

GROUND = 0x1000
COLLISION_SURFACE = 0x2000
NO_COLLISION_RESPONSE = 0x0010
KILL_PLANE = 0x0200
# A quadblock's 9 vertices: 0..3 corners, 4..8 edge and centre points; four sub-quads.
SUBQUADS = ((0, 4, 6, 5), (4, 1, 7, 6), (5, 6, 8, 2), (6, 7, 3, 8))

BG = "#1b181f"
BACKDROP = "#2a2530"
ROAD_SHADES = ("#5b5566", "#7a7385", "#9a93a4", "#bdb6c6")  # low to high
EDGE = "#121016"
PATH = "#f2ad55"
PINK = "#ff4fa3"
SMOOTH_CPS = 3        # height shading averages this many checkpoints either side
START_HALF_WIDTH = 1000  # longest half of the start line, in world units
OVERPASS = 400        # height gap for a dark crossing edge
AIR_GAP = 450         # --rings: a box anchor this far above the floor under it counts as high up
SUPPORT_SLOPE = 0.2   # floors for the "what is under the box" test may be this steep
FOOTPRINT = 50        # also test this far around the anchor (a crate is about 108 wide)
# Shortcut Knowledge tier per (track slug, box number), copied from the AP-Pie
# AP box pages (ap-web/guides/ctr-reference/ap-boxes.json, 2026-09-24).
SHORTCUT_TIERS = {
    ("tiger-temple", 5): "Medium", ("coco-park", 7): "Medium", ("dragon-mines", 2): "Medium",
    ("sewer-speedway", 2): "Medium", ("sewer-speedway", 3): "Medium",
    ("papus-pyramid", 6): "Medium", ("papus-pyramid", 12): "Medium",
    ("papus-pyramid", 7): "Hard", ("papus-pyramid", 10): "Hard", ("polar-pass", 9): "Hard",
    ("hot-air-skyway", 8): "Hard", ("oxide-station", 6): "Hard",
}


def subquads(info):
    """Yield (flags, on_path, polygon_xz, mean_y, normal_y, corners, checkpoint) per sub-quad.

    normal_y is the absolute y part of the unit normal (1 = flat, 0 = wall);
    corners are the four 3D points in polygon order."""
    verts = [v["position"] for v in info["geometry"]["vertices"]]
    for block in info["geometry"]["quadblocks"]:
        idx = block["vertex_indices"]
        for sub in SUBQUADS:
            pts = [verts[idx[k]] for k in sub]
            poly = Polygon([(p[0], p[2]) for p in pts])
            if not poly.is_valid:
                poly = poly.buffer(0)
            if poly.area < 1:
                continue
            a, b, c = pts[0], pts[1], pts[3]
            u = [b[i] - a[i] for i in range(3)]
            w = [c[i] - a[i] for i in range(3)]
            n = (u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0])
            length = math.sqrt(sum(x * x for x in n)) or 1
            yield (block["flags"], block["checkpoint_index"] != 255, poly, sum(p[1] for p in pts) / 4,
                   abs(n[1]) / length, pts, block["checkpoint_index"])


def main_loop(checkpoints):
    """Checkpoint indices in driving order, from 0 following forward links."""
    order, seen, i = [], set(), 0
    while i not in seen and i != 255 and i < len(checkpoints):
        seen.add(i)
        order.append(i)
        i = checkpoints[i]["links"]["forward"]
    return order


def fmt(v):
    return str(int(round(v)))


def path_d(geom, tol):
    """SVG path data for a (multi)polygon, simplified, integer coordinates."""
    geom = geom.simplify(tol, preserve_topology=True)
    polys = [geom] if geom.geom_type == "Polygon" else [g for g in getattr(geom, "geoms", []) if g.geom_type == "Polygon"]
    out = []
    for p in polys:
        if p.area < tol * tol * 4:
            continue
        for ring in [p.exterior, *p.interiors]:
            c = list(ring.coords)[:-1]
            if len(c) < 3:
                continue
            out.append("M" + " ".join(f"{fmt(x)} {fmt(y)}" for x, y in c) + "Z")
    return "".join(out)


def line_d(geom, tol):
    geom = geom.simplify(tol)
    lines = [geom] if geom.geom_type == "LineString" else [g for g in getattr(geom, "geoms", []) if g.geom_type == "LineString"]
    return "".join("M" + " ".join(f"{fmt(x)} {fmt(y)}" for x, y in g.coords) for g in lines if g.length > tol * 3)


def spread(points, r, badges=None, iterations=300):
    """Push marker centres apart so circles of radius r do not overlap much.

    badges[i] is True when marker i carries a shortcut badge, which needs a
    little more room."""
    pos = [list(p) for p in points]
    badges = badges or [False] * len(points)
    for _ in range(iterations):
        moved = False
        for i in range(len(pos)):
            for j in range(i + 1, len(pos)):
                dx, dy = pos[j][0] - pos[i][0], pos[j][1] - pos[i][1]
                d = math.hypot(dx, dy)
                gap = r * (2.15 + 0.5 * (badges[i] + badges[j]))
                if d < gap:
                    if d < 1e-6:
                        dx, dy, d = 1.0, 0.0, 1.0
                    push = (gap - d) / 2 + 1
                    pos[i][0] -= dx / d * push
                    pos[i][1] -= dy / d * push
                    pos[j][0] += dx / d * push
                    pos[j][1] += dy / d * push
                    moved = True
        # a light pull back towards the true spot keeps markers close
        for p, o in zip(pos, points):
            p[0] += (o[0] - p[0]) * 0.02
            p[1] += (o[1] - p[1]) * 0.02
        if not moved:
            break
    return pos


def floor_height(pts, x, z):
    """Height of a sub-quad at (x, z), or None when the point is outside it."""
    for a, b, c in ((pts[0], pts[1], pts[2]), (pts[0], pts[2], pts[3])):
        det = (b[2] - c[2]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[2] - c[2])
        if abs(det) < 1e-9:
            continue
        l1 = ((b[2] - c[2]) * (x - c[0]) + (c[0] - b[0]) * (z - c[2])) / det
        l2 = ((c[2] - a[2]) * (x - c[0]) + (a[0] - c[0]) * (z - c[2])) / det
        l3 = 1 - l1 - l2
        if min(l1, l2, l3) >= -1e-6:
            return l1 * a[1] + l2 * b[1] + l3 * c[1]
    return None


def classify_box(floors, x, y, z):
    """Where a box sits: ("road" | "air" | "drop", hover note). "road" means any track surface.

    Only a rough guide, which is why rings are off by default: boxes 200 to 300
    units above the road often sit on it in the pictures, and some platforms
    (Hot Air Skyway, Papu's Pyramid wall tops) are models, not quadblocks, so
    the floor under a box there is missed.

    Looks straight down from the anchor (and FOOTPRINT around it) for the
    highest floor at or just under the box. Kill planes do not count as floor.
    """
    polys, tree = floors
    best = None
    for dx, dz in ((0, 0), (FOOTPRINT, 0), (-FOOTPRINT, 0), (0, FOOTPRINT), (0, -FOOTPRINT)):
        px, pz = x + dx, z + dz
        pt = Point(px, pz)
        for i in tree.query(pt, predicate="intersects"):
            poly, pts, is_road = polys[i]
            h = floor_height(pts, px, pz)
            if h is None or h > y + 60:
                continue
            if best is None or h > best[0] + 1 or (abs(h - best[0]) <= 1 and is_road):
                best = (h, is_road)
    if best is None:
        return "drop", "no track surface under it"
    if y - best[0] > AIR_GAP:
        return "air", "high above the track surface under it"
    return "road", ""


def build(level, rings=False):
    name = shot.TRACKS[level]
    _, project = shot.project_for(level)
    info = shot.lev_info(project["sources"]["lev"]["path"])

    road_parts, backdrop_parts, floors = [], [], []
    for flags, on_path, poly, y, ny, pts, cp in subquads(info):
        if flags & KILL_PLANE:
            continue
        if ny >= SUPPORT_SLOPE and flags & (GROUND | COLLISION_SURFACE) or on_path:
            floors.append((poly, pts, bool(on_path or flags & GROUND)))
        if ny < 0.45:
            continue
        if on_path or flags & GROUND:
            road_parts.append((poly, y, cp if on_path else None))
        elif flags & COLLISION_SURFACE and not flags & NO_COLLISION_RESPONSE and ny > 0.7:
            backdrop_parts.append(poly)

    # Shade by the median height of all road sharing a checkpoint, so bumps and
    # ramps along the road (Slide Coliseum, Tiger Temple) do not stripe the map.
    by_cp = {}
    for _, y, cp in road_parts:
        if cp is not None:
            by_cp.setdefault(cp, []).append(y)
    median = {cp: sorted(v)[len(v) // 2] for cp, v in by_cp.items()}
    # then average over neighbouring checkpoints, so humps (Tiny Arena) blend in
    count = len(info["checkpoints"])
    smooth = {}
    for cp in median:
        near = [median[(cp + k) % count] for k in range(-SMOOTH_CPS, SMOOTH_CPS + 1) if (cp + k) % count in median]
        smooth[cp] = sum(near) / len(near)
    road_parts = [(p, smooth[cp] if cp is not None else y) for p, y, cp in road_parts]
    road = unary_union([p for p, _ in road_parts]).buffer(8).buffer(-8)
    minx, minz, maxx, maxz = road.bounds

    boxes = []
    n = 1
    while True:
        try:
            x, y, z, _ = shot.ap_box(level, n)
        except SystemExit:
            break
        boxes.append((n, x, y, z))
        n += 1
    for _, x, _, z in boxes:
        minx, maxx, minz, maxz = min(minx, x), max(maxx, x), min(minz, z), max(maxz, z)

    width = maxx - minx
    r = max(width, (maxz - minz) * 0.8) * 0.032   # marker radius in world units
    tol = r * 0.12
    pad = r * 1.6
    vb = (minx - pad, minz - pad, width + 2 * pad, maxz - minz + 2 * pad)

    near = road.buffer(r * 2.2)
    backdrop = unary_union(backdrop_parts).intersection(near).buffer(tol).buffer(-tol) if backdrop_parts else None

    ys = sorted(y for _, y in road_parts)
    cuts = [ys[int(len(ys) * q)] for q in (0.25, 0.5, 0.75)]
    lo, hi = ys[0], ys[-1]
    if hi - lo < 600:            # flat track: one shade
        cuts = []
    bands = [[] for _ in range(len(cuts) + 1)]
    for poly, y in road_parts:
        bands[sum(y > c for c in cuts)].append(poly)
    shades = ROAD_SHADES[-1:] if not cuts else ROAD_SHADES

    svg = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{" ".join(fmt(v) for v in vb)}" '
           f'role="img" aria-label="Top-down map of {name} with the AP item boxes numbered" '
           f'font-family="system-ui,-apple-system,Segoe UI,Roboto,sans-serif">',
           f'<title>{name} AP box map</title>',
           f'<rect x="{fmt(vb[0])}" y="{fmt(vb[1])}" width="{fmt(vb[2])}" height="{fmt(vb[3])}" fill="{BG}"/>']
    if backdrop is not None and not backdrop.is_empty:
        svg.append(f'<path fill="{BACKDROP}" d="{path_d(backdrop, tol * 2)}"/>')
    # checkpoint path, split per height band so a bridge hides the path underneath it
    cps = info["checkpoints"]
    loop = main_loop(cps)
    pts = [(cps[i]["position"][0], cps[i]["position"][2]) for i in loop]
    centre = LineString(pts + [pts[0]])

    def band_of(y):
        return sum(y > c for c in cuts)

    lines = [[] for _ in bands]
    ring = loop + loop[:1]
    for i, j in zip(ring, ring[1:]):
        a, b = cps[i]["position"], cps[j]["position"]
        lines[band_of((a[1] + b[1]) / 2)].append(LineString([(a[0], a[2]), (b[0], b[2])]))
    branch = [[] for _ in bands]
    for cp in cps:
        for key in ("left", "right"):
            j = cp["links"][key]
            if j != 255 and j < len(cps):
                a, b = cp["position"], cps[j]["position"]
                branch[band_of((a[1] + b[1]) / 2)].append(LineString([(a[0], a[2]), (b[0], b[2])]))
    arrows = [[] for _ in bands]
    seg_ends, total = [], 0.0
    for i, j in zip(ring, ring[1:]):
        a, b = cps[i]["position"], cps[j]["position"]
        total += math.hypot(b[0] - a[0], b[2] - a[2])
        seg_ends.append((total, (a[1] + b[1]) / 2))
    count = max(8, int(centre.length / (r * 6.5)))
    size = r * 0.55
    for s_ in range(1, count):
        d0 = centre.length * s_ / count
        p, q = centre.interpolate(d0), centre.interpolate(d0 + r * 0.5)
        ang = math.atan2(q.y - p.y, q.x - p.x)
        tip = (p.x + math.cos(ang) * size, p.y + math.sin(ang) * size)
        left = (p.x + math.cos(ang + 2.5) * size, p.y + math.sin(ang + 2.5) * size)
        right = (p.x + math.cos(ang - 2.5) * size, p.y + math.sin(ang - 2.5) * size)
        y = next((y for end, y in seg_ends if end >= d0), seg_ends[-1][1])
        arrows[band_of(y)].append(f"M{fmt(left[0])} {fmt(left[1])}L{fmt(tip[0])} {fmt(tip[1])}L{fmt(right[0])} {fmt(right[1])}")

    for k, parts in enumerate(bands):
        if parts:
            band = unary_union(parts).buffer(8).buffer(-8)
            svg.append(f'<path fill="{shades[min(k, len(shades) - 1)]}" d="{path_d(band, tol)}"/>')
            if k:
                # dark edge only where this band passes over road clearly lower down (a bridge or overpass)
                far_below = [p for p, y in road_parts if y < cuts[k - 1] - OVERPASS]
                if far_below:
                    over = band.boundary.intersection(unary_union(far_below).buffer(-r * 0.1))
                    d = line_d(over, tol) if not over.is_empty else ""
                    if d:
                        svg.append(f'<path fill="none" stroke="{EDGE}" stroke-width="{fmt(r * 0.16)}" d="{d}"/>')
        if lines[k]:
            svg.append(f'<path fill="none" stroke="{PATH}" stroke-opacity="0.7" stroke-width="{fmt(r * 0.1)}" '
                       f'stroke-dasharray="{fmt(r * 0.5)} {fmt(r * 0.35)}" d="{line_d(unary_union(lines[k]), tol)}"/>')
        if branch[k]:
            svg.append(f'<path fill="none" stroke="{PATH}" stroke-opacity="0.35" stroke-width="{fmt(r * 0.07)}" '
                       f'd="{line_d(unary_union(branch[k]), tol)}"/>')
        if arrows[k]:
            svg.append(f'<path fill="none" stroke="{PATH}" stroke-width="{fmt(r * 0.16)}" stroke-linecap="round" '
                       f'stroke-linejoin="round" d="{"".join(arrows[k])}"/>')

    # start line: perpendicular to the path at checkpoint 0, clipped to the road there
    p0 = Point(pts[0])
    back = centre.interpolate(centre.length - r * 0.4)
    fwd = centre.interpolate(r * 0.4)
    ang = math.atan2(fwd.y - back.y, fwd.x - back.x) + math.pi / 2
    span = LineString([(p0.x - math.cos(ang) * r * 12, p0.y - math.sin(ang) * r * 12),
                       (p0.x + math.cos(ang) * r * 12, p0.y + math.sin(ang) * r * 12)])
    cut = span.intersection(road.buffer(r * 0.1))
    parts = [cut] if cut.geom_type == "LineString" else list(getattr(cut, "geoms", []))
    parts = [g for g in parts if g.geom_type == "LineString"]
    if parts:
        seg = min(parts, key=lambda g: g.distance(p0))
        # never longer than a wide road: on some tracks the line runs along another road
        seg = seg.intersection(p0.buffer(START_HALF_WIDTH))
        if seg.geom_type != "LineString":
            seg = min((g for g in seg.geoms if g.geom_type == "LineString"), key=lambda g: g.distance(p0))
        (sx, sy), (ex, ey) = seg.coords[0], seg.coords[-1]
        w = r * 0.45
        svg.append(f'<path stroke="#f5f1eb" stroke-width="{fmt(w)}" d="M{fmt(sx)} {fmt(sy)}L{fmt(ex)} {fmt(ey)}"/>')
        svg.append(f'<path stroke="{EDGE}" stroke-width="{fmt(w)}" stroke-dasharray="{fmt(w / 2)}" '
                   f'd="M{fmt(sx)} {fmt(sy)}L{fmt(ex)} {fmt(ey)}"/>')
        tx, ty = ex + (ex - sx) / max(seg.length, 1) * r * 0.5, ey + (ey - sy) / max(seg.length, 1) * r * 0.5
        anchor = "start" if ex >= sx else "end"
        svg.append(f'<text x="{fmt(tx)}" y="{fmt(ty + r * 0.35)}" font-size="{fmt(r * 0.95)}" font-weight="700" '
                   f'fill="#f5f1eb" stroke="{BG}" stroke-width="{fmt(r * 0.25)}" paint-order="stroke" '
                   f'text-anchor="{anchor}">Start</text>')

    # box markers
    slug = shot.slug(name)
    floor_tree = STRtree([p for p, _, _ in floors]) if rings else None
    spots = []
    for n, x, y, z in boxes:
        kind, note = classify_box((floors, floor_tree), x, y, z) if rings else ("road", "")
        tier = SHORTCUT_TIERS.get((slug, n))
        if tier:
            note = "; ".join(filter(None, [note, "only with Shortcut Knowledge " + ("Medium or Hard" if tier == "Medium" else "Hard")]))
        spots.append((n, x, z, kind, note, tier))
    placed = spread([(x, z) for _, x, z, _, _, _ in spots], r, [bool(s[5]) for s in spots])
    leaders, marks = [], []
    for (n, x, z, kind, note, tier), (px, pz) in zip(spots, placed):
        if math.hypot(px - x, pz - z) > r * 0.6:
            leaders.append(f"M{fmt(x)} {fmt(z)}L{fmt(px)} {fmt(pz)}")
            marks.append(f'<circle cx="{fmt(x)}" cy="{fmt(z)}" r="{fmt(r * 0.22)}" fill="{PINK}" stroke="{BG}" stroke-width="{fmt(r * 0.08)}"/>')
        ring = (f'<circle cx="{fmt(px)}" cy="{fmt(pz)}" r="{fmt(r * 1.08)}" fill="none" stroke="#fff" '
                f'stroke-width="{fmt(r * 0.14)}" stroke-dasharray="{fmt(r * 0.32)} {fmt(r * 0.2)}"/>') if kind != "road" else ""
        badge = ""
        if tier:
            bx, by = px + r * 0.8, pz - r * 0.8
            badge = (f'<circle cx="{fmt(bx)}" cy="{fmt(by)}" r="{fmt(r * 0.52)}" fill="{PATH}" stroke="{BG}" '
                     f'stroke-width="{fmt(r * 0.1)}"/><text x="{fmt(bx)}" y="{fmt(by + r * 0.23)}" '
                     f'font-size="{fmt(r * 0.66)}" font-weight="800" fill="{EDGE}" text-anchor="middle">{tier[0]}</text>')
        marks.append(
            f'<a href="#box-{n}"><title>Item Box {n}{": " + note if note else ""}</title>'
            f'<circle cx="{fmt(px)}" cy="{fmt(pz)}" r="{fmt(r)}" fill="{PINK}" stroke="{BG}" stroke-width="{fmt(r * 0.14)}"/>{ring}'
            f'<text x="{fmt(px)}" y="{fmt(pz + r * 0.37)}" font-size="{fmt(r * (1.05 if n < 10 else 0.95))}" '
            f'font-weight="700" fill="#fff" text-anchor="middle">{n}</text>{badge}</a>')
    if leaders:
        svg.append(f'<path fill="none" stroke="{PINK}" stroke-width="{fmt(r * 0.1)}" d="{"".join(leaders)}"/>')
    svg.extend(marks)
    svg.append("</svg>")
    return shot.slug(name), "\n".join(svg) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--track", action="append", required=True, help="track name, slug, LevelID, or 'all' (repeatable)")
    ap.add_argument("--out", required=True, help="output folder; writes <slug>/<slug>-map.svg inside it")
    ap.add_argument("--rings", action="store_true",
                    help="ring boxes that look high up or have no track surface under them (rough; see classify_box)")
    args = ap.parse_args()
    levels = []
    for t in args.track:
        levels.extend(range(len(shot.TRACKS)) if t == "all" else [shot.track_id(t)])
    for level in levels:
        slug, svg = build(level, args.rings)
        path = Path(args.out) / slug / f"{slug}-map.svg"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(svg)
        print(f"{path}  {len(svg.encode()) // 1024} KB")


if __name__ == "__main__":
    main()
