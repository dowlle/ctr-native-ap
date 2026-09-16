from pathlib import Path
from contextlib import redirect_stdout
import io
import json
import struct
import tempfile
import unittest
from unittest.mock import patch

from levtool.retail import (track_info, prepare_project, validate_vrm, coordinate_record,
                            export_coordinates, file_sha256)
from tools.run_editor_project import main, build_command
from tests.lev_fixture import build_minimal_lev


def make_bigfile(path):
    blob = bytearray(2048)
    struct.pack_into("<ii", blob, 0, 0, 138)
    for level in (16, 17):
        for entry, data in ((level * 8, struct.pack("<IIIHHHH", 0x10, 2, 16, level, 0, 2, 1) + b"\0" * 4),
                            (level * 8 + 1, build_minimal_lev())):
            # Distinct fixture positions pin correct entry selection.
            if entry % 8 == 1:
                data = bytearray(data)
                instdef = struct.unpack_from("<I", data, 4 + 0x10)[0]
                struct.pack_into("<h", data, 4 + instdef + 0x30, level)
                data = bytes(data)
            struct.pack_into("<ii", blob, 8 + entry * 8, len(blob) // 2048, len(data))
            blob.extend(data)
            blob.extend(b"\0" * ((-len(blob)) % 2048))
    path.write_bytes(blob)


class RetailEditorTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.assets = self.root / "assets"
        self.assets.mkdir()
        self.source = self.assets / "BIGFILE.BIG"
        make_bigfile(self.source)
        self.projects = self.root / "projects"

    def prepare(self, track=16):
        return prepare_project(track, self.assets, self.projects)

    def test_catalog_and_invalid_selection(self):
        for value in (16, "16", "slide-coliseum", "Slide Coliseum"):
            self.assertEqual(track_info(value)["lev_entry"], 129)
        self.assertEqual(track_info("Turbo Track")["vrm_entry"], 136)
        for value in (-1, 18, "../track", "nonsense", True):
            with self.assertRaises(ValueError):
                track_info(value)

    def test_empty_independent_projects_and_complete_commands(self):
        paths = [self.prepare(n) for n in (16, 17)]
        projects = [json.loads(p.read_text()) for p in paths]
        for n, path, project in zip((16, 17), paths, projects):
            self.assertEqual(project["host_slot"], n)
            self.assertEqual(project["ap_candidates"], [])
            self.assertEqual(project["vanilla_objects"], [])
            command = build_command(self.root / "editor", path, project)
            self.assertEqual(command[command.index("--editor-host-slot") + 1], str(n))
            self.assertEqual(command[command.index("--editor-lev") + 1], project["sources"]["lev"]["path"])
        self.assertNotEqual(projects[0]["sources"], projects[1]["sources"])

    def test_reselection_preserves_candidates_camera_and_ordinals(self):
        path = self.prepare()
        other = self.prepare(17)
        other_bytes = other.read_bytes()
        source_hash = file_sha256(self.source)
        project = json.loads(path.read_text())
        project["ap_candidates"] = [{"id": f"ap-{i+1:04d}", "ordinal": i,
                                     "position": [i, -32768, 32767], "rotation": [0, 1024, 0]} for i in range(3)]
        project["camera"]["position"] = [1, 2, 3]
        path.write_text(json.dumps(project))
        before = path.read_bytes()
        self.assertEqual(self.prepare(), path)
        self.assertEqual(path.read_bytes(), before)
        self.assertEqual(other.read_bytes(), other_bytes)
        self.assertEqual(file_sha256(self.source), source_hash)
        record = coordinate_record(path)
        self.assertEqual(record["ap_candidates"], project["ap_candidates"])
        self.assertEqual(record["level_id"], 16)
        receipt = export_coordinates(path)
        self.assertEqual(receipt, export_coordinates(path))
        self.assertEqual(json.loads(receipt.read_text()), record)

    def test_invalid_path(self):
        with self.assertRaises(ValueError):
            prepare_project(16, self.root / "missing", self.projects)

    def test_corrupt_cache_is_not_overwritten(self):
        path = self.prepare()
        project = json.loads(path.read_text())
        lev = Path(project["sources"]["lev"]["path"])
        lev.write_bytes(b"corrupt")
        with self.assertRaisesRegex(ValueError, "hash"):
            self.prepare()
        self.assertEqual(lev.read_bytes(), b"corrupt")

    def test_cross_track_sidecar_refused_unchanged(self):
        path = self.prepare()
        project = json.loads(path.read_text())
        project["host_slot"] = 17
        path.write_text(json.dumps(project))
        before = path.read_bytes()
        with self.assertRaisesRegex(ValueError, "identity"):
            self.prepare()
        self.assertEqual(path.read_bytes(), before)

    def test_source_change_does_not_repin_existing_project(self):
        path = self.prepare()
        before = path.read_bytes()
        with self.source.open("ab") as stream:
            stream.write(b"changed")
        with self.assertRaisesRegex(ValueError, "identity"):
            self.prepare()
        self.assertEqual(path.read_bytes(), before)

    def test_truncated_or_out_of_range_entry(self):
        data = bytearray(self.source.read_bytes())
        struct.pack_into("<ii", data, 8 + 129 * 8, 999999, 20)
        self.source.write_bytes(data)
        with self.assertRaisesRegex(ValueError, "bounds"):
            self.prepare()
        self.assertFalse(self.projects.exists())

    def test_bad_lev_and_vrm_formats(self):
        for data in (b"", b"not a vrm", struct.pack("<I", 0x20), struct.pack("<II", 0x20, 0)):
            with self.assertRaises(ValueError):
                validate_vrm(data)
        data = bytearray(self.source.read_bytes())
        offset, _ = struct.unpack_from("<ii", data, 8 + 129 * 8)
        struct.pack_into("<I", data, offset * 2048, 0x7fffffff)
        self.source.write_bytes(data)
        with self.assertRaisesRegex(ValueError, "LEV"):
            self.prepare()

    def test_export_rejects_changed_hashes_or_invalid_ordinals(self):
        path = self.prepare()
        project = json.loads(path.read_text())
        project["ap_candidates"] = [{"id": "ap-0001", "ordinal": 2, "position": [0, 0, 0], "rotation": [0, 0, 0]}]
        path.write_text(json.dumps(project))
        with self.assertRaisesRegex(ValueError, "ordinals"):
            coordinate_record(path)

    def test_headless_print_never_launches(self):
        binary = self.root / "editor"
        binary.touch()
        with patch("tools.run_editor_project.run_editor_loop", side_effect=AssertionError("launched")):
            with redirect_stdout(io.StringIO()) as output:
                self.assertEqual(main(["--retail-track", "17", "--assets", str(self.assets),
                                       "--projects", str(self.projects), "--binary", str(binary), "--print-command"]), 0)
            command = json.loads(output.getvalue())
            self.assertEqual(command[command.index("--editor-host-slot") + 1], "17")

    def test_chooser_cancel_never_launches_or_writes(self):
        with patch("tools.run_editor_project.choose_retail_track", return_value=None), \
             patch("tools.run_editor_project.run_editor_loop", side_effect=AssertionError("launched")):
            self.assertEqual(main([]), 0)
        self.assertFalse(self.projects.exists())

    def make_disc(self, serial="SCUS_944.26"):
        bigfile = self.source.read_bytes()
        iso = bytearray(19 * 2048) + bytearray(bigfile)
        def record(name, lba, size, flags=0):
            name = name.encode("ascii")
            data = bytearray(33 + len(name) + (len(name) % 2 == 0))
            data[0] = len(data)
            struct.pack_into("<I", data, 2, lba)
            struct.pack_into("<I", data, 10, size)
            data[25], data[32] = flags, len(name)
            data[33:33 + len(name)] = name
            return data
        iso[16 * 2048:16 * 2048 + 7] = b"\x01CD001\x01"
        root = record("\0", 17, 2048, 2)
        iso[16 * 2048 + 156:16 * 2048 + 156 + len(root)] = root
        boot = f"BOOT = cdrom:\\{serial};1\n".encode("ascii")
        directory = record("SYSTEM.CNF;1", 18, len(boot)) + record("BIGFILE.BIG;1", 19, len(bigfile))
        iso[17 * 2048:17 * 2048 + len(directory)] = directory
        iso[18 * 2048:18 * 2048 + len(boot)] = boot
        assets = self.root / "disc-assets"
        assets.mkdir()
        raw = bytearray()
        for offset in range(0, len(iso), 2048):
            sector = bytearray(2352)
            sector[24:24 + 2048] = iso[offset:offset + 2048]
            raw.extend(sector)
        (assets / "ctr-u.bin").write_bytes(raw)
        return assets

    def test_raw_disc_matches_extracted_bigfile_pair(self):
        assets = self.make_disc()
        for level in (16, 17):
            direct = json.loads(self.prepare(level).read_text())
            disc = json.loads(prepare_project(level, assets, self.root / "disc-projects").read_text())
            self.assertEqual(disc["host_slot"], level)
            for kind in ("lev", "vrm"):
                self.assertEqual(direct["sources"][kind]["sha256"], disc["sources"][kind]["sha256"])

    def test_other_region_disc_fails_before_writing(self):
        assets = self.make_disc("SCES_021.05")
        with self.assertRaisesRegex(ValueError, "NTSC-U"):
            prepare_project(16, assets, self.projects)
        self.assertFalse(self.projects.exists())
