#!/usr/bin/env python3
"""Extract the game assets CTR-AP needs from your own Crash Team Racing disc.

CTR-AP ships no game data. To play, you provide a disc image of your own
NTSC-U (North American) Crash Team Racing PlayStation disc, and this tool copies
the handful of files the engine needs into an "assets" folder next to it.

Usage:
    python extract_assets.py MyDisc.cue
    python extract_assets.py MyDisc.bin --out path/to/play-folder/assets
    python extract_assets.py MyDisc.chd            (needs chdman on your PATH)

Requirements: Python 3.8 or newer. Nothing else to install (standard library
only). A .chd image additionally needs the "chdman" tool on your PATH.

The tool reads the disc's own file system (ISO9660), so it never guesses: it
pulls exactly the files the engine validates at startup, at their real sizes.
"""

import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile

# ---------------------------------------------------------------------------
# Asset manifest.
#
# Four files are always required. The engine also requires a set of XA audio
# tracks, and it does not hardcode which ones: it reads them from the XA
# manifest file (XA/ENG.XNF) on the disc. We parse that same manifest here so
# the extracted set matches exactly what the engine will look for.
# ---------------------------------------------------------------------------
CORE_FILES = [
    "BIGFILE.BIG",
    "SOUNDS/KART.HWL",
    "TEST.STR",
    "XA/ENG.XNF",
]

# XA category index -> on-disc directory. Order matches the engine's own table.
XA_DIRS = ["XA/MUSIC", "XA/ENG/EXTRA", "XA/ENG/GAME"]

# XA manifest (XNF) binary layout. Little-endian throughout.
XNF_MAGIC = 0x464E4958          # "XINF"
XNF_VERSION = 102
XNF_TYPE_COUNT = 3
XNF_HEADER_SIZE = 0x44
XNF_NUM_XAS_TOTAL_OFFSET = 0x0C
XNF_NUM_TRACKS_TOTAL_OFFSET = 0x10
XNF_NUM_SONGS_OFFSET = 0x2C
XNF_FIRST_SONG_INDEX_OFFSET = 0x38
XNF_ENTRY_BYTES = 4
XNF_MAX_FILE_NUMBER = 256


class ExtractError(Exception):
    """A user-facing error with an actionable message."""


# ---------------------------------------------------------------------------
# Raw disc reading.
#
# PlayStation discs are commonly dumped as raw 2352-byte sectors. CTR (NTSC-U)
# is MODE2/2352, where each sector carries 2048 bytes of user data at a
# 24-byte offset. We autodetect the sector layout by looking for the ISO9660
# "CD001" signature so plain 2048-byte ISO dumps and MODE1/2352 dumps also work.
# ---------------------------------------------------------------------------
USER = 2048
PVD_LBA = 16

# Mode 2 Form 2 payload size. Form 2 sectors (XA audio, STR video) carry 2324
# bytes of user data instead of 2048, and every CTR tool -- including the engine
# -- expects them as raw 2336-byte sectors: subheader(8) + user(2324) + EDC(4),
# i.e. everything after the 16-byte sync+header. Extracting them at 2048 leaves
# the file with the right sector COUNT but truncated contents, so XA audio and
# STR video decode to silence or garbage while every size check still passes.
FORM2_USER = 2336
FORM2_OFFSET = 16
# Submode bit 5 in the Mode 2 subheader marks a Form 2 sector.
SUBMODE_FORM2 = 0x20

# (raw sector size, offset of user data within the sector), most specific first.
SECTOR_LAYOUTS = [
    (2352, 24),   # MODE2/2352 form 1 (the usual CTR dump)
    (2352, 16),   # MODE1/2352
    (2048, 0),    # plain ISO9660 (2048-byte sectors)
]


class Disc:
    def __init__(self, path):
        self.path = path
        self.file = open(path, "rb")
        self.raw = None
        self.offset = None
        self._detect_layout()

    def _detect_layout(self):
        for raw, off in SECTOR_LAYOUTS:
            self.file.seek(PVD_LBA * raw + off)
            data = self.file.read(6)
            if len(data) >= 6 and data[1:6] == b"CD001":
                self.raw = raw
                self.offset = off
                return
        raise ExtractError(
            "This file does not look like a PlayStation disc image.\n"
            "  No ISO9660 file system was found. Make sure you pointed the\n"
            "  tool at the actual disc image (a .bin, .cue, or .chd), not a\n"
            "  zip or a folder."
        )

    def read_sector(self, lba):
        self.file.seek(lba * self.raw + self.offset)
        return self.file.read(USER)

    def read_extent(self, lba, size):
        out = bytearray()
        nsec = (size + USER - 1) // USER
        for s in range(nsec):
            out += self.read_sector(lba + s)
        return bytes(out[:size])

    def is_form2(self, lba):
        """True when this sector is Mode 2 Form 2 (XA audio / STR video).

        Read from the sector's own subheader rather than guessing from the file
        extension, so a Form 1 file is never converted by mistake. Only raw
        2352-byte dumps carry a subheader at all; a plain 2048-byte ISO has
        already discarded the Form 2 payload, so it cannot be recovered here.
        """
        if self.raw != 2352:
            return False
        self.file.seek(lba * self.raw + FORM2_OFFSET)
        sub = self.file.read(8)
        return len(sub) == 8 and bool(sub[2] & SUBMODE_FORM2)

    def read_extent_form2(self, lba, nsec):
        """Read nsec Form 2 sectors as raw 2336-byte units.

        The ISO9660 directory record sizes a Form 2 file in 2048-byte units, so
        the SECTOR COUNT comes from that record while each sector is emitted at
        its true 2336 bytes. This is why a truncated extraction still produced a
        file with a plausible size: the sector count was right and only the
        per-sector payload was short.
        """
        out = bytearray()
        for s in range(nsec):
            self.file.seek((lba + s) * self.raw + FORM2_OFFSET)
            chunk = self.file.read(FORM2_USER)
            if len(chunk) != FORM2_USER:
                break
            out += chunk
        return bytes(out)

    def close(self):
        try:
            self.file.close()
        except Exception:
            pass


# ---------------------------------------------------------------------------
# ISO9660 directory tree.
# ---------------------------------------------------------------------------
def parse_dir_records(data):
    """Yield (name, lba, size, is_dir) from a directory extent's bytes."""
    i = 0
    while i < len(data):
        rec_len = data[i]
        if rec_len == 0:
            nxt = (i // USER + 1) * USER
            if nxt <= i:
                break
            i = nxt
            continue
        rec = data[i:i + rec_len]
        if len(rec) < 33:
            break
        ext_lba = struct.unpack("<I", rec[2:6])[0]
        size = struct.unpack("<I", rec[10:14])[0]
        flags = rec[25]
        name_len = rec[32]
        name = rec[33:33 + name_len]
        is_dir = bool(flags & 0x02)
        nm = name.decode("latin-1")
        if not is_dir:
            nm = nm.split(";")[0]
        yield nm, ext_lba, size, is_dir
        i += rec_len


def build_index(disc):
    """Walk the whole tree once, returning:
       files: dict of UPPERCASE "/PATH" -> (lba, size)
       root_names: list of top-level entry names (for region detection)
       volume_id: the ISO volume identifier string
    """
    pvd = disc.read_sector(PVD_LBA)
    if pvd[1:6] != b"CD001":
        raise ExtractError("Could not read the disc's primary volume descriptor.")
    volume_id = pvd[40:72].decode("latin-1").strip()
    root = pvd[156:156 + 34]
    root_lba = struct.unpack("<I", root[2:6])[0]
    root_size = struct.unpack("<I", root[10:14])[0]

    files = {}
    root_names = []

    def walk(lba, size, path, depth):
        if depth > 16:
            return
        data = disc.read_extent(lba, size)
        for name, ext_lba, sz, is_dir in parse_dir_records(data):
            if not name or name in ("\x00", "\x01", ".", ".."):
                continue
            full = path + "/" + name
            if depth == 0:
                root_names.append(name)
            if is_dir:
                walk(ext_lba, sz, full, depth + 1)
            else:
                files[full.upper()] = (ext_lba, sz)

    walk(root_lba, root_size, "", 0)
    return files, root_names, volume_id


def find_file(files, rel_path):
    """Look up a manifest-relative path (e.g. 'XA/ENG.XNF') in the index."""
    return files.get("/" + rel_path.upper())


# ---------------------------------------------------------------------------
# Region detection.
# ---------------------------------------------------------------------------
def boot_serial_from_system_cnf(disc, files):
    entry = find_file(files, "SYSTEM.CNF")
    if entry is None:
        return None
    data = disc.read_extent(entry[0], entry[1])
    text = data.decode("latin-1", "ignore")
    for line in text.splitlines():
        line = line.strip()
        if line.upper().startswith("BOOT"):
            # e.g. "BOOT = cdrom:\SCUS_944.26;1"
            value = line.split("=", 1)[1] if "=" in line else ""
            value = value.strip()
            base = value.replace("\\", "/").split("/")[-1]
            base = base.split(";")[0]
            return base.strip()
    return None


def normalize_serial(text):
    return "".join(ch for ch in text.upper() if ch.isalnum())


def detect_region(disc, files, volume_id, root_names):
    """Return ('NTSC-U'|'PAL'|'NTSC-J'|'UNKNOWN', detail_string)."""
    # Prefer the boot serial named in SYSTEM.CNF; fall back to root file names
    # and the volume id.
    serial = boot_serial_from_system_cnf(disc, files)
    candidates = []
    if serial:
        candidates.append(serial)
    candidates.extend(root_names)
    candidates.append(volume_id)

    ntsc_u = ("SCUS", "SLUS", "SLUSP")
    pal = ("SCES", "SLES", "SCED", "SLED")
    ntsc_j = ("SCPS", "SLPS", "SLPM", "SCAJ")

    for cand in candidates:
        norm = normalize_serial(cand)
        for prefix in ntsc_u:
            if norm.startswith(prefix):
                return "NTSC-U", (serial or cand)
        for prefix in pal:
            if norm.startswith(prefix):
                return "PAL", (serial or cand)
        for prefix in ntsc_j:
            if norm.startswith(prefix):
                return "NTSC-J", (serial or cand)
    return "UNKNOWN", (serial or volume_id or "(unrecognized)")


# ---------------------------------------------------------------------------
# XA manifest parsing (mirrors the engine's own validation logic).
# ---------------------------------------------------------------------------
def _u32(data, off):
    return struct.unpack_from("<I", data, off)[0]


def xa_files_from_manifest(xnf):
    """Return the list of manifest-relative XA paths the engine requires,
    parsed from the XA/ENG.XNF bytes exactly as the engine parses them."""
    if len(xnf) < XNF_HEADER_SIZE:
        raise ExtractError("The XA manifest (XA/ENG.XNF) on this disc is too small to be valid.")
    if _u32(xnf, 0) != XNF_MAGIC or _u32(xnf, 4) != XNF_VERSION or _u32(xnf, 8) != XNF_TYPE_COUNT:
        raise ExtractError("The XA manifest (XA/ENG.XNF) on this disc is not the expected format.")

    num_xas_total = _u32(xnf, XNF_NUM_XAS_TOTAL_OFFSET)
    num_tracks_total = _u32(xnf, XNF_NUM_TRACKS_TOTAL_OFFSET)
    entry_offset = XNF_HEADER_SIZE + num_xas_total * 4
    entry_end = entry_offset + num_tracks_total * XNF_ENTRY_BYTES
    if entry_end < entry_offset or entry_end > len(xnf):
        raise ExtractError("The XA manifest (XA/ENG.XNF) on this disc has an invalid track table.")

    wanted = []
    for category in range(XNF_TYPE_COUNT):
        num_songs = _u32(xnf, XNF_NUM_SONGS_OFFSET + category * 4)
        first_song = _u32(xnf, XNF_FIRST_SONG_INDEX_OFFSET + category * 4)
        if first_song > num_tracks_total or num_songs > num_tracks_total \
                or first_song + num_songs > num_tracks_total:
            raise ExtractError("The XA manifest (XA/ENG.XNF) on this disc has an invalid song range.")
        for i in range(num_songs):
            entry = entry_offset + (first_song + i) * XNF_ENTRY_BYTES
            file_number = xnf[entry + 1]
            if file_number >= XNF_MAX_FILE_NUMBER:
                continue
            wanted.append("%s/S%02d.XA" % (XA_DIRS[category], file_number))
    # De-duplicate while preserving order.
    seen = set()
    ordered = []
    for path in wanted:
        if path not in seen:
            seen.add(path)
            ordered.append(path)
    return ordered


# ---------------------------------------------------------------------------
# Input handling: .cue / .bin / .chd -> a raw .bin path to read.
# ---------------------------------------------------------------------------
def bin_from_cue(cue_path):
    """Return the .bin path referenced by a .cue, and warn on unexpected mode."""
    with open(cue_path, "r", errors="replace") as f:
        text = f.read()
    bin_name = None
    for line in text.splitlines():
        s = line.strip()
        if s.upper().startswith("FILE"):
            # FILE "name.bin" BINARY
            first = s.find('"')
            last = s.rfind('"')
            if first != -1 and last > first:
                bin_name = s[first + 1:last]
            break
    if not bin_name:
        raise ExtractError(
            "Could not read a data file from the .cue sheet.\n"
            "  Point the tool at the .bin directly instead: python extract_assets.py MyDisc.bin"
        )
    resolved = os.path.join(os.path.dirname(os.path.abspath(cue_path)), bin_name)
    if not os.path.isfile(resolved):
        raise ExtractError(
            "The .cue sheet points at '%s', but that file is not next to it.\n"
            "  Keep the .cue and .bin together, or run the tool on the .bin directly." % bin_name
        )
    return resolved


def bin_from_chd(chd_path, workdir):
    """Convert a .chd to .bin/.cue using chdman, returning the .bin path."""
    chdman = shutil.which("chdman")
    if not chdman:
        raise ExtractError(
            "This is a .chd image, which needs the 'chdman' tool to unpack.\n"
            "  chdman is not on your PATH. You have two options:\n"
            "    1. Install chdman (it ships with MAME tools), then run this again.\n"
            "    2. Convert the .chd to .bin/.cue yourself, then run this on the .cue."
        )
    out_cue = os.path.join(workdir, "disc.cue")
    print("Unpacking .chd with chdman (this can take a minute)...")
    result = subprocess.run(
        [chdman, "extractcd", "-i", chd_path, "-o", out_cue],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
    )
    if result.returncode != 0:
        detail = result.stdout.decode("latin-1", "ignore").strip()
        raise ExtractError(
            "chdman could not unpack this .chd:\n  %s" % (detail or "(no output)")
        )
    return bin_from_cue(out_cue)


def resolve_bin(image_path, workdir):
    ext = os.path.splitext(image_path)[1].lower()
    if ext == ".cue":
        return bin_from_cue(image_path)
    if ext == ".chd":
        return bin_from_chd(image_path, workdir)
    # Treat anything else (.bin, .img, .iso, extensionless) as a raw image.
    return image_path


# ---------------------------------------------------------------------------
# Extraction.
# ---------------------------------------------------------------------------
def write_file(disc, out_dir, rel_path, lba, size):
    dest = os.path.join(out_dir, rel_path.replace("/", os.sep))
    os.makedirs(os.path.dirname(dest), exist_ok=True)

    # Mode 2 Form 2 extents (XA audio, STR video) must be written as raw 2336-byte
    # sectors. Reading them at 2048 like a Form 1 file keeps the sector count but
    # truncates every sector, so the engine loads them without error and then plays
    # silence or garbage. Detected from the sector subheader, not the extension.
    if disc.is_form2(lba):
        nsec = (size + USER - 1) // USER
        data = disc.read_extent_form2(lba, nsec)
        expected = nsec * FORM2_USER
        if len(data) != expected:
            raise ExtractError(
                "Short read while extracting %s (disc image may be truncated)." % rel_path)
        with open(dest, "wb") as f:
            f.write(data)
        return len(data)

    data = disc.read_extent(lba, size)
    if len(data) != size:
        raise ExtractError("Short read while extracting %s (disc image may be truncated)." % rel_path)
    with open(dest, "wb") as f:
        f.write(data)
    return len(data)


def extract(image_path, out_dir):
    workdir = tempfile.mkdtemp(prefix="ctr_extract_")
    disc = None
    try:
        bin_path = resolve_bin(image_path, workdir)
        if not os.path.isfile(bin_path):
            raise ExtractError("Disc image not found: %s" % bin_path)

        disc = Disc(bin_path)
        files, root_names, volume_id = build_index(disc)

        region, detail = detect_region(disc, files, volume_id, root_names)
        print("Disc volume id: %s" % (volume_id or "(none)"))
        print("Detected region: %s (%s)" % (region, detail))

        if region == "PAL":
            raise ExtractError(
                "This looks like a PAL (European) disc. PAL is not supported yet.\n"
                "  CTR-AP currently needs an NTSC-U (North American) disc, whose boot\n"
                "  id starts with SCUS. Support for other regions may come later."
            )
        if region == "NTSC-J":
            raise ExtractError(
                "This looks like a Japanese (NTSC-J) disc, which is not supported.\n"
                "  CTR-AP currently needs an NTSC-U (North American) disc (boot id SCUS)."
            )
        if region == "UNKNOWN":
            raise ExtractError(
                "Could not confirm this is an NTSC-U (North American) CTR disc.\n"
                "  CTR-AP needs the North American release (boot id SCUS_94426).\n"
                "  If you are sure this is the right disc, please report it so region\n"
                "  detection can be improved."
            )

        # Build the full manifest: core files, then the XA set from the manifest.
        xnf_entry = find_file(files, "XA/ENG.XNF")
        if xnf_entry is None:
            raise ExtractError(
                "The XA manifest (XA/ENG.XNF) is missing from this disc.\n"
                "  This does not look like a complete CTR disc image."
            )
        xnf_bytes = disc.read_extent(xnf_entry[0], xnf_entry[1])
        xa_paths = xa_files_from_manifest(xnf_bytes)
        manifest = CORE_FILES + xa_paths

        # Verify everything is present before writing anything.
        missing = [p for p in manifest if find_file(files, p) is None]
        if missing:
            raise ExtractError(
                "This disc image is missing files CTR-AP needs:\n    "
                + "\n    ".join(missing)
                + "\n  The image may be an incomplete or non-standard dump."
            )

        os.makedirs(out_dir, exist_ok=True)
        total_bytes = 0
        print("\nExtracting %d files to: %s" % (len(manifest), os.path.abspath(out_dir)))
        for rel in manifest:
            lba, size = find_file(files, rel)
            written = write_file(disc, out_dir, rel, lba, size)
            total_bytes += written
            print("  %-28s %12d bytes" % (rel, written))

        # Final verification: files exist on disk with the expected sizes.
        problems = []
        for rel in manifest:
            _, size = find_file(files, rel)
            dest = os.path.join(out_dir, rel.replace("/", os.sep))
            if not os.path.isfile(dest):
                problems.append("%s was not written" % rel)
            elif os.path.getsize(dest) != size:
                problems.append("%s has the wrong size" % rel)
            elif size == 0:
                problems.append("%s is empty" % rel)
        if problems:
            raise ExtractError("Verification failed after extraction:\n    " + "\n    ".join(problems))

        print("\nDone. Extracted %d files (%.1f MB) with no errors."
              % (len(manifest), total_bytes / (1024.0 * 1024.0)))
        print("Place this 'assets' folder next to ctr_native_ap.exe, then run the game.")
        return 0
    finally:
        if disc is not None:
            disc.close()
        shutil.rmtree(workdir, ignore_errors=True)


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Extract CTR-AP game assets from your own NTSC-U Crash Team Racing disc image.",
    )
    parser.add_argument("image", help="Path to your disc image (.cue, .bin, or .chd).")
    parser.add_argument(
        "--out",
        default=os.path.join(os.getcwd(), "assets"),
        help="Where to write the assets folder (default: ./assets in the current directory).",
    )
    args = parser.parse_args(argv)

    if not os.path.isfile(args.image):
        print("Error: file not found: %s" % args.image, file=sys.stderr)
        return 2

    try:
        return extract(args.image, args.out)
    except ExtractError as exc:
        print("\nError: %s" % exc, file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("\nInterrupted.", file=sys.stderr)
        return 130


if __name__ == "__main__":
    sys.exit(main())
