#!/usr/bin/env python3
"""Report fallback crates farther than 512 units from eligible geometry.

Requires numpy, Pillow and an editor checkout with levtool and editor-shots.
Run under the project's heavy-job lock, with no more than two renderer threads.
Accepts LEV paths or an A3-33 results.json corpus (paths and paired VRMs).
Writes only to --out. Never edits exclusions, packages, seeds or source LEVs.
Missing VRMs or failed pictures are reported and cause a nonzero exit status.
Use --measure-only for a measurement report without rendering.
"""
import argparse
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import sys

from crate_geometry import triangles, measure


def picture(track, crate, source, out):
    from levtool.project import new_project
    spec = importlib.util.spec_from_file_location("crate_shot", source / "tools/editor-shots/shot.py")
    shot = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(shot)
    lev, vrm = Path(track["path"]), Path(track["vrm"])
    if not vrm.is_file():
        raise ValueError("no paired VRM file")
    project = new_project(lev, vrm, 3)
    tag = track["sha256"] + "-crate" + str(crate["ordinal"])
    project_path = out / (tag + ".editor.json")
    project_path.write_text(json.dumps(project, indent=2))
    p, q = crate["pos"], crate["nearest"]
    if q is None:
        q = [p[0] + 500, p[1], p[2]]
    center = [(a + b) / 2 for a, b in zip(p, q)]
    dx, dz = p[0] - q[0], p[2] - q[2]
    distance = math.hypot(dx, dz)
    if distance < 1:
        dx, dz, distance = 1, 0, 1
    radius = max(1050, distance * .9)
    eye = [center[0] + dz / distance * radius, max(p[1], q[1]) + max(650, distance * .5),
           center[2] - dx / distance * radius]
    png = out / (tag + ".png")
    shot.render(project_path, project, eye, shot.look_rotation(eye, center),
                [(p[0], p[1] + 54, p[2], 0)], png, start=60,
                video=dict(shot.VIDEO, window="1280x720"))
    (out / (tag + ".camera.json")).write_text(json.dumps(dict(eye=eye, look=center, crate=crate), indent=2))
    return png.name


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("lev", type=Path, nargs="*")
    parser.add_argument("--corpus", type=Path, help="A3-33 results.json, or this tool's report.json")
    parser.add_argument("--editor-source", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--measure-only", action="store_true")
    args = parser.parse_args()
    sys.path.insert(0, str(args.editor_source.resolve()))
    from levtool.format import LevFile
    os.environ["LP_NUM_THREADS"] = "2"
    os.environ["OMP_NUM_THREADS"] = "2"
    args.out.mkdir(parents=True, exist_ok=True)
    inputs = [dict(path=str(p.resolve()), vrm=str(p.resolve().with_suffix(".vrm"))) for p in args.lev]
    if args.corpus:
        corpus = json.loads(args.corpus.read_text())
        inputs += corpus if isinstance(corpus, list) else corpus["tracks"]
    if not inputs:
        parser.error("supply LEV files or --corpus")
    results, seen, errors = [], set(), 0
    for track in inputs:
        row = dict(path=track["path"], vrm=track.get("vrm", str(Path(track["path"]).with_suffix(".vrm"))))
        try:
            digest = hashlib.sha256(Path(row["path"]).read_bytes()).hexdigest()
            if track.get("sha256", digest) != digest:
                raise ValueError("corpus digest disagrees with LEV bytes")
            if digest in seen:
                continue
            seen.add(digest)
            row["sha256"] = digest
            lev = LevFile.from_path(row["path"])
            if not lev.vertices or not lev.quadblocks:
                raise ValueError("no track geometry")
            ts, ids = triangles(lev)
            crates = []
            for ordinal, instance in enumerate((i for i in lev.instances if i.model_id == 8), 1):
                value = dict(ordinal=ordinal, pos=instance.position, **measure(instance.position, ts, ids))
                value["flagged"] = value["horizontal"] > 512
                if value["flagged"] and not args.measure_only:
                    try:
                        value["picture"] = picture(row, value, args.editor_source, args.out)
                    except (Exception, SystemExit) as exc:
                        value["picture_error"] = str(exc)
                        errors += 1
                crates.append(value)
            row["crates"] = crates
        except Exception as exc:
            row["error"] = str(exc)
            errors += 1
        results.append(row)
        print(row.get("sha256", row["path"]), len(row.get("crates", [])), flush=True)
    report = dict(threshold=512, above=384, below=128, report_only=True, errors=errors, tracks=results)
    (args.out / "report.json").write_text(json.dumps(report, indent=2, allow_nan=False))
    lines = ["# Crates flagged for review", "", "512-unit geometry rule. Pictures are editor views; reachability needs review.", ""]
    for row in results:
        lines += ["## " + row.get("sha256", "Unreadable LEV"), ""]
        if "error" in row:
            lines += ["Error: " + row["error"], ""]
        for crate in row.get("crates", []):
            if crate["flagged"]:
                lines += [f"Crate {crate['ordinal']}, position {crate['pos']}, distance {crate['horizontal']:.1f} units.", ""]
                if "picture" in crate:
                    lines += [f"![Crate {crate['ordinal']}]({crate['picture']})", ""]
                elif "picture_error" in crate:
                    lines += ["Picture failed: " + crate["picture_error"], ""]
    (args.out / "report.md").write_text("\n".join(lines))
    return bool(errors)


if __name__ == "__main__":
    sys.exit(main())
