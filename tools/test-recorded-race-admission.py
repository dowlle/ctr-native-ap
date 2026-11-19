"""Exercise the real package acquisition, selector and runtime with local fixtures."""

import argparse
import copy
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import subprocess
from tempfile import TemporaryDirectory
import uuid

ROOT = Path(__file__).resolve().parents[1]
FILE_NAMES = {
    "lev": "track.lev",
    "vrm": "track.vrm",
    "recorded_ai": "recorded-ai.navset",
    "race_grid": "grid-spots.json",
    "weapon_crate_spots": "weapon-crate-spots.json",
    "race_settings": "race-settings.json",
}
TRACK_UUID = uuid.UUID("0f0e0d0c-0b0a-4908-8706-123456789abc")


def digest(data):
    return hashlib.sha256(data).hexdigest()


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":")).encode()


def build_fixture_lev(*, arcade=False):
    mesh, quads, vertices, checkpoints = 0x190, 0x1b0, 0x20c, 0x29c
    nav, header, map_offset = 0x2cc, 0x2d8, 0x350
    body = bytearray(map_offset)
    struct.pack_into("<I", body, 0, mesh)
    struct.pack_into("<II", body, 0x148, 4, checkpoints)
    struct.pack_into("<IIIII", body, mesh, 1, 9, 0, quads, vertices)
    struct.pack_into("<10H", body, quads, *range(9), 0x1000)
    coords = [(-1000, -1000), (1000, -1000), (-1000, 1000), (1000, 1000),
              (0, -1000), (-1000, 0), (0, 0), (1000, 0), (0, 1000)]
    for i, (x, z) in enumerate(coords):
        struct.pack_into("<3h", body, vertices + 16 * i, x, 0, z)
    checkpoint_positions = [(0, 1000), (1000, 0), (0, -1000), (-1000, 0)]
    for i, (x, z) in enumerate(checkpoint_positions):
        ahead = (i + 1) % 4
        struct.pack_into("<3hH4B", body, checkpoints + 12 * i, x, 0, z,
                         (4 - i) * 100, ahead, 255, (i - 1) % 4, 255)
    slots = [0, 0x14c, mesh + 12, mesh + 16]
    if arcade:
        struct.pack_into("<I", body, 0x188, nav)
        struct.pack_into("<I", body, nav, header)
        struct.pack_into("<hH", body, header, -0x1303, 1)
        slots += [0x188, nav]
        for i in range(8):
            struct.pack_into("<h", body, 0x6c + i * 12, i + 1)
    return struct.pack("<I", map_offset) + body + struct.pack(f"<{len(slots) + 1}I", len(slots) * 4, *slots)


def course_nodes(lev):
    """Sample the forward checkpoint edges for the optional real-track fixture."""
    count, start = struct.unpack_from("<II", lev, 4 + 0x148)
    entries = [struct.unpack_from("<3hH4B", lev, 4 + start + i * 12) for i in range(count)]
    result = []
    checkpoint = 0
    for _ in range(count):
        current = entries[checkpoint]
        next_checkpoint = current[4]
        following = entries[next_checkpoint]
        for step in range(4):
            pos = tuple(round((a * (4 - step) + b * step) / 4) for a, b in zip(current[:3], following[:3]))
            result.append((*pos, checkpoint))
        checkpoint = next_checkpoint
    assert checkpoint == 0
    return result


def fixture_navset(lev_hash, *, count=7, wrong_id=False, wrong_revision=False,
                   dirty=False, duplicate=False, open_lap=False,
                   bad_checkpoint=False, reverse_cycle=False,
                   midcourse=False, reverse_geometry=False, course=None):
    nodes = len(course) if course else 32
    data = bytearray(128 + 64 * count)
    struct.pack_into("<4sHH", data, 0, b"NAV2", 4, 128)
    data[0x55], data[0x56], data[0x60] = count, 20, 1
    data[0x64:0x74] = uuid.UUID(int=1).bytes if wrong_id else TRACK_UUID.bytes
    struct.pack_into("<I", data, 0x74, int(lev_hash[:8], 16) ^ int(wrong_revision))
    for lap in range(count):
        entry, start = 128 + 64 * lap, len(data)
        struct.pack_into("<5I", data, entry, nodes, 900, 900, start, start + nodes * 20)
        data[entry + 20] = int(not dirty)
        name = f"Harness {lap + 1}".encode()
        data[entry + 28:entry + 28 + len(name)] = name
        radius = 1000 + (0 if duplicate else 20 * lap)
        for i in range(nodes):
            index = (i + nodes // 2) % nodes if midcourse else i
            if course:
                x, y, z, checkpoint = course[index]
                x += lap * 4
            else:
                angle = math.tau * (-index if reverse_geometry else index) / nodes
                x, y, z = round(radius * math.sin(angle)), 0, round(radius * math.cos(angle))
                checkpoint = (index // 8) % 4
            if reverse_cycle:
                checkpoint = (-checkpoint) % 4
            if bad_checkpoint and i == 0:
                checkpoint = 255
            if open_lap and i == nodes - 1:
                x += 3000
            data.extend(struct.pack("<3h4B4h2B", x, y, z, 0, lap, 0, 0,
                                    200, 200, 0, 0x7fff, checkpoint, 0))
        data.extend(struct.pack(f"<{nodes}I", *(900 * i // nodes for i in range(nodes))))
    struct.pack_into("<I", data, 8, len(data) + 8)
    checksum = 0xcbf29ce484222325
    for value in data:
        checksum = ((checksum ^ value) * 0x100000001b3) & ((1 << 64) - 1)
    return data + struct.pack("<Q", checksum)


def package(root, files):
    root.mkdir()
    records = []
    for role, content in sorted(files.items()):
        path = FILE_NAMES[role]
        (root / path).write_bytes(content)
        records.append(dict(role=role, path=path, bytes=len(content), sha256=digest(content)))
    manifest = canonical(dict(schema_version=1, package_uuid=str(TRACK_UUID), version="1.0.0",
                              title="Wumpa World", compatibility=dict(native_contract=1, apworld_contract=1),
                              files=records))
    (root / "manifest.json").write_bytes(manifest)
    return digest(manifest)


def build_exe(root, lev_hash=None, vrm_hash=None):
    flags = ["-m32", "-O0", "-ffunction-sections", "-fdata-sections",
             "-DCTR_CUSTOM_TRACKS", "-DCTR_CUSTOM_PACKAGES", "-DCTR_RECORDED_RACE",
             "-DUTF8PROC_STATIC", "-Iinclude", "-I.", "-Iap/vendor/json/include", "-Ivendor/utf8proc"]
    if lev_hash is not None:
        flags += ["-DCTR_RECORDED_RACE_TEST_PINS", f'-DCTR_RECORDED_RACE_TEST_LEV="{lev_hash}"',
                  f'-DCTR_RECORDED_RACE_TEST_VRM="{vrm_hash}"']
    exe = root / "admission"
    cc, cxx = os.environ.get("CC", "cc"), os.environ.get("CXX", "c++")
    sources = ["tools/fixtures/recorded-race-admission.cpp", "platform/native_custom_package.cpp",
               "platform/native_custom_package_owned.c", "platform/native_custom_offline.c",
               "platform/native_custom_content_verify.c", "platform/native_custom_unicode.c",
               "vendor/utf8proc/utf8proc.c"]
    objects = []
    for source in map(Path, sources):
        obj = root / source.with_suffix(".o").name
        compiler = [cxx, "-std=c++17"] if source.suffix == ".cpp" else [cc]
        subprocess.run([*compiler, *flags, "-c", source, "-o", obj], check=True, cwd=ROOT)
        objects.append(obj)
    subprocess.run([cxx, "-m32", *objects, "-lm", "-Wl,--gc-sections", "-o", exe], check=True, cwd=ROOT)
    return exe


def verify(exe, root, files, expected, corrupt=None):
    pin = package(root, files)
    if corrupt:
        path = root / FILE_NAMES[corrupt]
        data = path.read_bytes()
        path.write_bytes(bytes([data[0] ^ 1]) + data[1:])
    result = subprocess.check_output([exe, root, pin], text=True).strip()
    assert result == expected, (root.name, result, expected)
    print(f"{root.name}: {result}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--wumpa-fixtures", type=Path, help="optional directory with original local track inputs")
    args = parser.parse_args()
    scratch = args.wumpa_fixtures.parent if args.wumpa_fixtures else None
    with TemporaryDirectory(prefix="ctr-recorded-race-", dir=scratch) as tmp:
        root = Path(tmp)
        lev, vrm = build_fixture_lev(), struct.pack("<3I4H", 0, 0, 0, 0, 0, 1, 1) + b"\0\0"
        lev_hash, vrm_hash = digest(lev), digest(vrm)
        exe = build_exe(root, lev_hash, vrm_hash)
        track = dict(kind="custom", track_key="wumpa-world", name="Wumpa World", lev_sha256=[lev_hash])
        grid = dict(format="ctr-ap-grid-spots", version=1, track=track,
                    spawns=[dict(pos=[20 * i, 0, 0], rot=[0, 0, 0]) for i in range(8)])
        rows = [dict(row=r + 1, source="fixture", credit="", review="",
                     crates=[dict(pos=[20 * i, 0, r * 40 + 40], rot_y=0) for i in range(4)]) for r in range(4)]
        crates = dict(format="ctr-ap-weapon-crate-spots", version=1, track=track, rows=rows)
        valid = dict(lev=lev, vrm=vrm, recorded_ai=fixture_navset(lev_hash), race_grid=canonical(grid),
                     weapon_crate_spots=canonical(crates))
        verify(exe, root / "valid", valid, "Recorded Arcade admitted")
        verify(exe, root / "ordinary", dict(valid, lev=build_fixture_lev(arcade=True)), "Ordinary Arcade admitted")
        verify(exe, root / "unknown-vrm", dict(valid, vrm=vrm[:-1] + b"\x01"),
               "Wrong VRM hash")
        verify(exe, root / "bad-lev", dict(valid, lev=lev[:4]), "LEV too small")
        verify(exe, root / "bad-vrm", dict(valid, vrm=vrm[:-1]), "VRM pixel size invalid")
        laps = dict(schema_version=1, lev_sha256=lev_hash, vrm_sha256=vrm_hash, laps=8)
        verify(exe, root / "unsupported-laps", dict(valid, race_settings=canonical(laps)),
               "Lap count not supported")
        for role, reason in [("recorded_ai", "Missing recordings"), ("race_grid", "Missing grid"),
                             ("weapon_crate_spots", "Missing crates")]:
            verify(exe, root / f"missing-{role}", {k: v for k, v in valid.items() if k != role}, reason)
            verify(exe, root / f"damaged-{role}", valid, f"Package {role} bytes do not match pin", role)
        for role in ("lev", "vrm"):
            verify(exe, root / f"damaged-{role}", valid, f"Package {role} bytes do not match pin", role)
        wrong_lev = bytearray(lev)
        wrong_lev[0x30] ^= 1
        verify(exe, root / "unknown-lev", dict(valid, lev=wrong_lev), "Wrong LEV hash")
        verify(exe, root / "ordinary-shared-vrm", dict(lev=wrong_lev, vrm=vrm), "No AI paths")
        verify(exe, root / "unrelated", dict(valid, lev=wrong_lev, vrm=vrm[:-1] + b"\x01"), "No AI paths")
        for flags, expected in [(dict(count=6), "Need seven laps"), (dict(wrong_id=True), "Wrong recording ID"),
                                (dict(wrong_revision=True), "Wrong recording ID"), (dict(dirty=True), "Unusable lap"),
                                (dict(open_lap=True), "Unusable lap"), (dict(duplicate=True), "Duplicate laps"),
                                (dict(bad_checkpoint=True), "Wrong lap checkpoints"),
                                (dict(reverse_cycle=True), "Wrong lap checkpoints"),
                                (dict(midcourse=True), "Lap starts off grid"),
                                (dict(reverse_geometry=True), "Lap leaves course")]:
            nav = fixture_navset(lev_hash, **flags)
            verify(exe, root / next(iter(flags)), dict(valid, recorded_ai=nav), expected)
        verify(exe, root / "bad-navset", dict(valid, recorded_ai=valid["recorded_ai"][:-1]), "Invalid recordings")
        verify(exe, root / "bad-grid", dict(valid, race_grid=canonical(dict(grid, spawns=[]))), "Invalid grid file")
        verify(exe, root / "bad-crates", dict(valid, weapon_crate_spots=canonical(dict(crates, rows=[]))),
               "Invalid crate file")
        off_grid, off_crates = copy.deepcopy(grid), copy.deepcopy(crates)
        off_grid["spawns"][0]["pos"][1] = 500
        off_crates["rows"][0]["crates"][0]["pos"][2] = -2000
        verify(exe, root / "off-grid", dict(valid, race_grid=canonical(off_grid)), "Grid off ground")
        verify(exe, root / "off-crates", dict(valid, weapon_crate_spots=canonical(off_crates)), "Crates off ground")
        if args.wumpa_fixtures:
            folder = args.wumpa_fixtures
            lev = (folder / "wumpa-world.lev").read_bytes()
            files = dict(lev=lev, vrm=(folder / "wumpa-world.vrm").read_bytes(),
                         recorded_ai=fixture_navset(digest(lev), course=course_nodes(lev)),
                         race_grid=(folder / "grid-spots.json").read_bytes(),
                         weapon_crate_spots=(folder / "weapon-crate-spots.json").read_bytes())
            base = root / "wumpa-base"
            pin = package(base, {role: files[role] for role in ("lev", "vrm")})
            base = base.rename(root / pin)
            sidecars = root / "sidecars"
            sidecars.mkdir()
            for role in ("recorded_ai", "race_grid", "weapon_crate_spots"):
                (sidecars / FILE_NAMES[role]).write_bytes(files[role])
            staged = Path(subprocess.check_output(
                ["python3", ROOT / "tools/navrec/stage-wumpa-world.py", base, sidecars, root],
                text=True).strip())
            result = subprocess.check_output([build_exe(root), staged, staged.name], text=True).strip()
            assert result == "Recorded Arcade admitted", result
            print(f"Staged Wumpa World {staged.name}: {result}")


if __name__ == "__main__":
    main()
