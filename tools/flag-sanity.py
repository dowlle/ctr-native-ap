#!/usr/bin/env python3
"""Measure all three ordinary pickup families against ordinary road geometry.

Writes a report and a compact needs catalog. Flagged pickups stay USF until
reviewed. Uses the same 512-unit distance and height slab as flag-crates.py.
No LEV bytes or positions are written into the shipping catalog.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import sys

from crate_geometry import triangles, measure


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--corpus", type=Path, required=True)
    p.add_argument("--editor-source", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--pictures", action="store_true")
    args = p.parse_args()
    sys.path.insert(0, str(args.editor_source))
    from levtool.format import LevFile
    spec = importlib.util.spec_from_file_location("flag_crates", Path(__file__).with_name("flag-crates.py"))
    flag = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(flag)
    args.out.mkdir(parents=True, exist_ok=True)
    catalog, report, seen = {}, [], set()
    # Explicit retail map uses the engine LevelID order.
    retail = ["Dingo Canyon", "Dragon Mines", "Blizzard Bluff", "Crash Cove", "Tiger Temple", "Papu's Pyramid",
              "Roo's Tubes", "Hot Air Skyway", "Sewer Speedway", "Mystery Caves", "Cortex Castle", "N. Gin Labs",
              "Polar Pass", "Oxide Station", "Coco Park", "Tiny Arena", "Slide Coliseum", "Turbo Track"]
    for source in json.loads(args.corpus.read_text()):
        path = Path(source["path"])
        if not path.is_file():
            continue
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        if digest in seen:
            continue
        seen.add(digest)
        row = dict(name=source["name"], path=str(path), vrm=source.get("vrm"), sha256=digest, objects=[])
        try:
            lev = LevFile.from_path(path)
            ts, ids = triangles(lev)
            entry = dict(lev_sha256=digest, counts=[], needs={})
            for family, model in (("weapon_crate", 8), ("wumpa_crate", 7), ("wumpa_fruit", 2)):
                objects = [i for i in lev.instances if i.model_id == model]
                entry["counts"].append(len(objects))
                for ordinal, inst in enumerate(objects, 1):
                    value = dict(family=family, ordinal=ordinal, pos=inst.position, **measure(inst.position, ts, ids))
                    # Four fruit crates in Tiger Temple's raised door corridor.
                    # Keep these conservative until the picture review, even
                    # though the corridor itself is flagged as road geometry.
                    door = source.get("retail") and source["name"] == "Tiger Temple" and family == "wumpa_crate" and ordinal in (5, 6, 7, 8)
                    value["flagged"] = value["horizontal"] > 512 or door
                    if door:
                        value["reason"] = "Tiger Temple door corridor, pending review"
                    if value["flagged"]:
                        entry["needs"].setdefault(family, {})[str(ordinal)] = dict(needs="usf", reviewed=False)
                        if door:
                            entry["needs"][family][str(ordinal)]["door"] = True
                        if args.pictures:
                            # Family-specific subdirectory avoids matching ordinal filenames.
                            folder = args.out / digest / family
                            folder.mkdir(parents=True, exist_ok=True)
                            try:
                                value["picture"] = str(folder / flag.picture(row, value, args.editor_source, folder))
                            except (Exception, SystemExit) as exc:
                                value["picture_error"] = str(exc)
                    row["objects"].append(value)
            catalog[digest] = entry
            if source.get("retail") and source["name"] in retail:
                catalog["retail:" + str(retail.index(source["name"]))] = entry
            if source["name"] == "Cortex Vortex":
                catalog["cortex_vortex"] = entry
        except Exception as exc:
            row["error"] = str(exc)
        report.append(row)
        print(row["name"], len(row["objects"]), row.get("error", ""), flush=True)
    (args.out / "catalog.json").write_text(json.dumps(catalog, indent=2) + "\n")
    (args.out / "report.json").write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
    return int(any("error" in r or any("picture_error" in o for o in r["objects"]) for r in report))


if __name__ == "__main__":
    sys.exit(main())
