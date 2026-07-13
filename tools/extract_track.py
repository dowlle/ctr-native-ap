#!/usr/bin/env python3
"""Custom-track spike helper: inspect and dump BIGFILE.BIG track subfile groups.

This works on an already-extracted BIGFILE.BIG (the file tools/extract-assets/
extract_assets.py writes into the "assets" folder). It never touches disc images
or ISO9660 -- extract_assets.py already did that step. See the BIGFILE layout,
verified against a real NTSC-U BIGFILE.BIG, in parse_bigfile() below.

Modes:
    extract_track.py diff  <clean BIGFILE.BIG> <patched BIGFILE.BIG>
        Compare every subfile and report which indices changed, grouped by the
        arcade track (index // 8) they belong to. This tells you which track
        slot a community xdelta patch actually replaced.

    extract_track.py dump  <BIGFILE.BIG> <levelID> <outdir>
        Write an arcade track's 8-subfile group to <outdir>/0.bin .. 7.bin.

    extract_track.py check <dir>
        Sanity-check a dumped group: 8 files present (0.bin..7.bin), all nonzero.

Arcade tracks are levelID 0..17. Each occupies 8 contiguous BIGFILE subfiles at
[levelID*8, levelID*8 + 8) because BI_ARCADETRACKS is 0 (include/namespace_Load.h).

Python 3.8+, standard library only.
"""

import os
import struct
import sys

# Arcade-track levelID -> name (include/namespace_Level.h, enum LevelID 0..17;
# NITRO_COURT == 18 is the first battle arena, not an arcade track).
TRACK_NAMES = [
    "Dingo Canyon",    # 0
    "Dragon Mines",    # 1
    "Blizzard Bluff",  # 2
    "Crash Cove",      # 3
    "Tiger Temple",    # 4
    "Papu Pyramid",    # 5
    "Roo Tubes",       # 6
    "Hot Air Skyway",  # 7
    "Sewer Speedway",  # 8
    "Mystery Caves",   # 9
    "Cortex Castle",   # 10
    "N. Gin Labs",     # 11
    "Polar Pass",      # 12
    "Oxide Station",   # 13
    "Coco Park",       # 14
    "Tiny Arena",      # 15
    "Slide Coliseum",  # 16
    "Turbo Track",     # 17
]

NUM_ARCADE_TRACKS = 18
GROUP_SIZE = 8         # subfiles per arcade-track group
SECTOR_SIZE = 2048     # BigEntry.offset is a sector count; data lives at offset*2048


def track_name(level_id):
    if 0 <= level_id < len(TRACK_NAMES):
        return TRACK_NAMES[level_id]
    return "levelID %d (not an arcade track)" % level_id


def parse_bigfile(path):
    """Return (cdpos, entries) where entries is a list of (offset_sectors, size).

    BIGFILE.BIG layout (verified against a real NTSC-U dump):
      struct BigHeader { int cdpos; int numEntry; };   // 8 bytes at file start
      struct BigEntry  { int offset; int size; };       // numEntry entries follow
    cdpos is overwritten at load time with the on-disc position, so its on-file
    value (0 in the retail dump) is not meaningful here. offset is a SECTOR count:
    a subfile's bytes are file[offset*2048 : offset*2048 + size].
    """
    with open(path, "rb") as f:
        header = f.read(8)
        if len(header) < 8:
            raise SystemExit("Error: %s is too small to be a BIGFILE.BIG" % path)
        cdpos, num_entry = struct.unpack("<ii", header)
        if num_entry <= 0 or num_entry > 100000:
            raise SystemExit(
                "Error: %s has an implausible entry count (%d); is this really a "
                "BIGFILE.BIG?" % (path, num_entry))
        dir_bytes = f.read(num_entry * 8)
        if len(dir_bytes) < num_entry * 8:
            raise SystemExit("Error: %s directory is truncated" % path)
    entries = [struct.unpack_from("<ii", dir_bytes, i * 8) for i in range(num_entry)]
    return cdpos, entries


def read_subfile(path, entries, index):
    """Read subfile `index`'s raw bytes (exactly .size bytes) from the BIGFILE."""
    offset_sectors, size = entries[index]
    with open(path, "rb") as f:
        f.seek(offset_sectors * SECTOR_SIZE)
        data = f.read(size)
    if len(data) != size:
        raise SystemExit(
            "Error: short read for subfile %d (%d of %d bytes) in %s"
            % (index, len(data), size, path))
    return data


def mode_diff(clean_path, patched_path):
    _, clean = parse_bigfile(clean_path)
    _, patched = parse_bigfile(patched_path)

    if len(clean) != len(patched):
        print("Note: entry counts differ (clean %d, patched %d); comparing the "
              "shared range." % (len(clean), len(patched)))
    n = min(len(clean), len(patched))

    changed = []
    for i in range(n):
        # Compare by (size, bytes). Size mismatch alone already means a change.
        if clean[i] != patched[i]:
            changed.append(i)
        else:
            cb = read_subfile(clean_path, clean, i)
            pb = read_subfile(patched_path, patched, i)
            if cb != pb:
                changed.append(i)

    if not changed:
        print("No subfile differences found between the two BIGFILEs.")
        return 0

    # Group changed indices by track (idx // 8) for arcade-track indices (< 144).
    by_track = {}
    other = []
    for idx in changed:
        track = idx // GROUP_SIZE
        if idx < NUM_ARCADE_TRACKS * GROUP_SIZE:
            by_track.setdefault(track, []).append(idx)
        else:
            other.append(idx)

    print("Changed subfile indices grouped by arcade track:")
    for track in sorted(by_track):
        slots = [idx - track * GROUP_SIZE for idx in by_track[track]]
        print("  levelID %-2d  %-16s  subfiles %s  (indices %s)"
              % (track, track_name(track),
                 ",".join(str(s) for s in slots),
                 ",".join(str(i) for i in by_track[track])))
        if len(by_track[track]) == GROUP_SIZE:
            print("             -> full group replaced; dump with: "
                  "extract_track.py dump <patched> %d tracks/<name>" % track)
    if other:
        print("Changed indices outside the arcade-track range (>= 144): %s"
              % ",".join(str(i) for i in other))
    return 0


def mode_dump(bigfile_path, level_id, outdir):
    if not (0 <= level_id < NUM_ARCADE_TRACKS):
        raise SystemExit("Error: levelID must be 0..%d (an arcade track)"
                         % (NUM_ARCADE_TRACKS - 1))
    _, entries = parse_bigfile(bigfile_path)
    base = level_id * GROUP_SIZE
    if base + GROUP_SIZE > len(entries):
        raise SystemExit("Error: BIGFILE has only %d entries; track %d needs "
                         "indices %d..%d" % (len(entries), level_id, base,
                                             base + GROUP_SIZE - 1))
    os.makedirs(outdir, exist_ok=True)
    print("Dumping %s (levelID %d) subfiles %d..%d to %s"
          % (track_name(level_id), level_id, base, base + GROUP_SIZE - 1, outdir))
    for slot in range(GROUP_SIZE):
        idx = base + slot
        data = read_subfile(bigfile_path, entries, idx)
        dest = os.path.join(outdir, "%d.bin" % slot)
        with open(dest, "wb") as f:
            f.write(data)
        print("  %d.bin  <- subfile %-4d  %10d bytes" % (slot, idx, len(data)))
    print("Done. Map it in config.ini with:  custom_track_%d = %s"
          % (level_id, outdir))
    return 0


def mode_check(dirpath):
    problems = []
    for slot in range(GROUP_SIZE):
        p = os.path.join(dirpath, "%d.bin" % slot)
        if not os.path.isfile(p):
            problems.append("%d.bin is missing" % slot)
        elif os.path.getsize(p) == 0:
            problems.append("%d.bin is empty" % slot)
    if problems:
        print("Group check FAILED for %s:" % dirpath)
        for p in problems:
            print("  - %s" % p)
        return 1
    print("Group check OK: %s has all 8 subfiles (0.bin..7.bin), all nonzero."
          % dirpath)
    for slot in range(GROUP_SIZE):
        p = os.path.join(dirpath, "%d.bin" % slot)
        print("  %d.bin  %10d bytes" % (slot, os.path.getsize(p)))
    return 0


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    mode = argv[1]
    if mode == "diff":
        if len(argv) != 4:
            raise SystemExit("Usage: extract_track.py diff <clean BIGFILE> <patched BIGFILE>")
        return mode_diff(argv[2], argv[3])
    if mode == "dump":
        if len(argv) != 5:
            raise SystemExit("Usage: extract_track.py dump <BIGFILE> <levelID> <outdir>")
        return mode_dump(argv[2], int(argv[3]), argv[4])
    if mode == "check":
        if len(argv) != 3:
            raise SystemExit("Usage: extract_track.py check <dir>")
        return mode_check(argv[2])
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
