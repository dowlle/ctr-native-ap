# Fallback crate review report

`flag-crates.py` implements the A3-33 candidate geometry rule. It reports
weapon crates farther than 512 horizontal LEV units from eligible collision
triangles clipped to crate Y minus 384 through crate Y plus 128. Eligible
surfaces have GROUND (0x1000), lack NO_COLLISION_RESPONSE (0x10), TRIGGER
(0x40) and KILL_PLANE (0x200), and are no steeper than 60 degrees. It uses
the eight high-detail COLL.c triangles, four on a collapsed triangle block.
Water terrain alone does not disqualify a surface. This is a review heuristic,
not a reachability or physics certificate. Reviewed hand-placed AP spots must
never be filtered with it.

Install numpy and Pillow in your tooling environment. Point `--editor-source`
at a compatible editor checkout containing `levtool` and
`tools/editor-shots/shot.py`. The editor's normal `CTR_EDITOR_BINARY` and
`CTR_EDITOR_ASSETS` environment settings apply. Each loose LEV uses its
same-stem VRM. A `--corpus` JSON can instead list `path`, `vrm` and optional
`sha256` per revision, as in the A3-33 report. Inputs are deduplicated by
SHA-256; supplied pins are verified before measurement.

```sh
python3 tools/flag-crates.py --editor-source /path/to/editor \
  --out /path/to/report /path/to/track.lev
```

Use the project's build/render lock and low priority when rendering. The tool
renders serially with at most two Mesa threads. `--measure-only` skips the
editor. Outputs include `report.json`, `report.md`, and, for every flag, an
editor PNG, project and camera JSON. Missing VRMs and render failures are
recorded and return a nonzero exit code; a flag without a picture is not visual
acceptance. Some automatic camera views may be occluded and need manual review.

The tool writes only to its output directory. It never updates the curated
list, packages, player data or seeds. It is not shipped in the player client.
The world owns `data/crate_exclusions.json` and carries applicable entries in
the seed under `item_boxes.crate_exclusions`, with required feature
`crate_exclusions_v1`. Native validates their LEV pins, ordinal bounds and
positions before seating. North Pole crate 1 (LEV 1.1.0) joined the list by owner decision; Roo's Tubes crate 9 remains a flag for review.

Saved rooms without this feature retain their original crate seating. This tool does not migrate rooms or rewrite their checks.
