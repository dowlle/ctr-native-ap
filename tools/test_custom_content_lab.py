#!/usr/bin/env python3

import argparse
import json
import struct
import tempfile
import unittest
import io
import zlib
from unittest import mock
from pathlib import Path

import custom_content_lab as lab


def make_lev(path: Path) -> None:
    body_inst, body_check, body_nav, body_map = 0x220, 0x500, 0x700, 0x1200
    nav_heads = (0x740, 0x880, 0x9c0)
    slots = (0x10, 0x14c, 0x188, body_nav, body_nav + 4, body_nav + 8)
    data = bytearray(4 + body_map + 4 + len(slots) * 4)
    def put(offset, value): struct.pack_into("<I", data, offset, value)
    put(0, body_map)
    models = (0x15, 0x23, 0x08, 0x5c, 0x93, 0x94, 0x95)
    put(4 + 0x0c, len(models)); put(4 + 0x10, body_inst); put(4 + 0x148, 35); put(4 + 0x14c, body_check)
    for index, model in enumerate(models): put(4 + body_inst + index * 0x40 + 0x3c, model)
    for index in range(8): data[4 + 0x6c + index * 12] = index + 1
    for index in range(35):
        data[4 + body_check + index * 12 + 8:4 + body_check + index * 12 + 12] = b"\xff" * 4
        data[4 + body_check + index * 12 + 8] = (index + 1) % 35
    put(4 + 0x188, body_nav)
    for index, head in enumerate(nav_heads):
        put(4 + body_nav + index * 4, head)
        struct.pack_into("<hH", data, 4 + head, -0x1303, 4)
    put(4 + body_map, len(slots) * 4)
    for index, slot in enumerate(slots): put(4 + body_map + 4 + index * 4, slot)
    path.write_bytes(data)


class LabTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="ctr-lab-test-")
        self.root = Path(self.temp.name)
        self.verifier = Path("/tmp/custom-content-verify")
        self.assertTrue(self.verifier.exists(), "compile /tmp/custom-content-verify first")

    def tearDown(self): self.temp.cleanup()

    def response(self):
        def media(mid, kind, modes):
            return {"id": mid, "type": kind, "version": "1.0.0", "modes": modes,
                    "file_size": 1, "crc32_hash": mid, "download_url": f"/file/{mid}"}
        return {"data": [
            {"id": 10, "name": "Fixture", "author": "Tester", "author_id": 2,
             "collection": "community", "collection_id": 2, "type": "race", "type_id": 1,
             "is_active": True, "downloads": [media(100, "lev", ["time_trial"]), media(101, "vrm", ["time_trial"])]},
            {"id": 11, "name": "Mismatch", "author": "Tester", "author_id": 2,
             "collection": "community", "collection_id": 2, "type": "race", "type_id": 1,
             "is_active": True, "downloads": [media(110, "lev", ["arcade"]), media(111, "vrm", [])]},
            {"id": 12, "name": "Hub", "author": "Tester", "author_id": 2,
             "collection": "community", "collection_id": 2, "type": "adventure_hub", "type_id": 3,
             "is_active": True, "downloads": []}]}

    def test_end_to_end_offline(self):
        catalogue = lab.catalogue_from_response(self.response())
        self.assertEqual(len(catalogue["tracks"]), 2)
        self.assertTrue(catalogue["tracks"][0]["versions"][0]["consistent"])
        self.assertEqual(len(catalogue["tracks"][1]["versions"]), 2)
        self.assertTrue(all(not row["consistent"] for row in catalogue["tracks"][1]["versions"]))
        lab.atomic_json(self.root / "catalogue.json", catalogue)
        lev, vrm = lab.cache_paths(self.root, 10, "1.0.0", 100, 101)
        lev.parent.mkdir(parents=True)
        make_lev(lev); vrm.write_bytes(b"\0" * 16 + b"\1\0\1\0\0\0")
        lab.command_analyze(argparse.Namespace(root=self.root, verifier=self.verifier, offline=True))
        inv = lab.inventory(self.root)
        self.assertEqual(len(inv["entries"]), 3)
        good = inv["entries"][0]
        self.assertEqual(good["status"], "verified")
        self.assertEqual(good["verification"]["file_analysis"]["time_trial"]["result"], "Detected")
        self.assertEqual([row["status"] for row in inv["entries"][1:]], ["inconsistent", "inconsistent"])
        good["lev_crc32"] = lab.file_crc32(self.root / good["lev_path"])
        good["vrm_crc32"] = lab.file_crc32(self.root / good["vrm_path"])
        lab.atomic_json(self.root / "inventory.json", inv)
        manifest = self.root / "session" / "manifest.json"
        lab.command_prepare_session(argparse.Namespace(root=self.root, variant_id=good["variant_id"],
                                    mode="time_trial", host=6, minutes=30))
        prepared = json.loads(manifest.read_text())
        self.assertEqual(prepared["variant_id"], good["variant_id"])
        self.assertEqual(prepared["host"], 6)
        executable = self.root / "ctr_native_ap"; executable.write_bytes(b"build")
        receipt = dict(prepared, executable_sha256=lab.sha256_file(executable),
                       git_commit="a" * 40, dirty=False, complete=True,
                       feature_configuration="test", start_time_unix=1, end_time_unix=2,
                       executable_path=str(executable.resolve()))
        receipt_path = self.root / "session" / "receipt.json"; lab.atomic_json(receipt_path, receipt)
        manifest.replace(self.root / "session" / "armed.json")
        collect_args = argparse.Namespace(root=self.root, executable=executable, reviewer="Tester",
            steam_attested=False, result="Yes", notes="fixture pass")
        with self.assertRaisesRegex(SystemExit, "attestation"):
            lab.command_collect_session(collect_args)
        wrong_executable = self.root / "wrong-build"; wrong_executable.write_bytes(b"other")
        collect_args.steam_attested = True; collect_args.executable = wrong_executable
        with self.assertRaisesRegex(SystemExit, "hash mismatch"):
            lab.command_collect_session(collect_args)
        lab.command_collect_session(argparse.Namespace(root=self.root,
            executable=executable, reviewer="Tester", steam_attested=True,
            result="Yes", notes="fixture pass"))
        record = lab.verdicts(self.root)["verdicts"][good["variant_id"]]
        self.assertEqual(record["lev_sha256"], good["verification"]["lev_sha256"])
        self.assertEqual(record["modes"]["time_trial"]["launch_method"], "user-steam-attested")
        with self.assertRaisesRegex(SystemExit, "required"):
            lab.command_collect_session(argparse.Namespace(root=self.root, executable=executable,
                reviewer="Tester", steam_attested=True, result="Yes", notes="reuse"))
        lab.command_export(argparse.Namespace(root=self.root))
        current = json.loads((self.root / "exports/current.json").read_text())
        generation = self.root / "exports" / current["generation_id"]
        out_json, out_xlsx = generation / "report.json", generation / "report.xlsx"
        exported = json.loads(out_json.read_text())
        self.assertEqual(exported["entries"][0]["time_trial In-game test"], "Yes")
        self.assertTrue(out_xlsx.exists() and out_xlsx.stat().st_size > 0)

    def test_untrusted_boundaries(self):
        with self.assertRaises(ValueError): lab.positive_int(True, "id")
        with self.assertRaises(ValueError): lab.saphi_url("https://evil.invalid/file")
        with self.assertRaises(ValueError): lab.saphi_url("//evil.invalid/file")
        lev, vrm = lab.cache_paths(self.root, 1, "../../escape", 2, 3)
        self.assertEqual(lev.parent, self.root / "media" / "1")
        self.assertEqual(vrm.parent, self.root / "media" / "1")

    def test_formula_text_is_neutralized(self):
        rows = [{"name": " =1+1", "author": "\t+cmd", "collection": "-2+3",
                 "version": "@SUM(A1)", "notes": " =cmd", "existing_saphi_tags": "+tag",
                 "status": "verified"}]
        original = lab.rows_for_export
        lab.rows_for_export = lambda root: rows
        try:
            lab.command_export(argparse.Namespace(root=self.root))
            current = json.loads((self.root / "exports/current.json").read_text())
            generation = self.root / "exports" / current["generation_id"]
            out_xlsx = generation / "report.xlsx"
            from openpyxl import load_workbook
            workbook = load_workbook(out_xlsx, data_only=False)
            values = [cell for row in workbook.active.iter_rows(min_row=2) for cell in row]
            self.assertTrue(all(cell.data_type == "s" for cell in values))
            self.assertTrue(values[0].value.startswith("'"))
            export_manifest = json.loads((generation / "manifest.json").read_text())
            self.assertTrue(export_manifest["xlsx_text_escaped"])
            self.assertFalse(workbook._external_links)
        finally:
            lab.rows_for_export = original

    def test_download_rejects_bad_cache_and_bad_range(self):
        payload = b"trusted bytes"
        media = {"file_size": len(payload), "crc32_hash": f"{zlib.crc32(payload) & 0xffffffff:08x}",
                 "download_url": "/media/test"}
        target = self.root / "media/1/lev-2.lev"
        target.parent.mkdir(parents=True)
        target.write_bytes(b"x" * len(payload))

        class Response(io.BytesIO):
            def __init__(self, data, status=200, headers=None):
                super().__init__(data); self.status = status; self.headers = headers or {}
            def __enter__(self): return self
            def __exit__(self, *args): self.close()

        with mock.patch.object(lab.urllib.request, "urlopen", return_value=Response(payload)):
            lab.download_media(media, target)
        self.assertEqual(target.read_bytes(), payload)
        self.assertTrue(list(target.parent.glob("*.quarantine-*-integrity")))

        target.unlink()
        partial = target.with_suffix(".lev.part")
        partial.write_bytes(payload[:3])
        response = Response(payload[3:], 206, {"Content-Range": f"bytes 4-{len(payload)-1}/{len(payload)}"})
        with mock.patch.object(lab.urllib.request, "urlopen", return_value=response):
            with self.assertRaisesRegex(IOError, "Content-Range"):
                lab.download_media(media, target)

    def test_serialized_identity_mismatch_is_rejected(self):
        lab.atomic_json(self.root / "inventory.json", {"schema_version": 2, "entries": [{
            "track_id": 1, "version": "1.0.0", "lev_media_id": 2, "vrm_media_id": 3,
            "variant_id": "0" * 64}]})
        with self.assertRaisesRegex(ValueError, "variant_id"):
            lab.inventory(self.root)

    def test_atomic_json_faults_leave_one_complete_document(self):
        path = self.root / "state.json"
        old, new = {"generation": "old", "rows": [1, 2]}, {"generation": "new", "rows": [3, 4]}
        lab.atomic_json(path, old)
        stages = ("before_write", "after_write", "after_flush", "after_file_sync",
                  "before_replace", "after_replace", "after_directory_sync")
        try:
            for stage in stages:
                lab.atomic_json(path, old)
                def fail(current, target, wanted=stage):
                    if current == wanted and target == path: raise RuntimeError(wanted)
                lab._fault_hook = fail
                with self.assertRaisesRegex(RuntimeError, stage): lab.atomic_json(path, new)
                self.assertIn(json.loads(path.read_text()), (old, new))
                self.assertFalse(list(path.parent.glob(f".{path.name}.*.tmp")))
                lab._fault_hook = None
        finally:
            lab._fault_hook = None

    def test_failed_workbook_never_publishes_half_generation(self):
        original = lab.rows_for_export
        lab.rows_for_export = lambda root: [{"track_id": 1, "name": "stable"}]
        try:
            lab.command_export(argparse.Namespace(root=self.root))
            before = json.loads((self.root / "exports/current.json").read_text())
            with mock.patch("openpyxl.workbook.workbook.Workbook.save", side_effect=RuntimeError("xlsx fault")):
                with self.assertRaisesRegex(RuntimeError, "xlsx fault"):
                    lab.command_export(argparse.Namespace(root=self.root))
            after = json.loads((self.root / "exports/current.json").read_text())
            self.assertEqual(after, before)
            generation = self.root / "exports" / before["generation_id"]
            manifest = json.loads((generation / "manifest.json").read_text())
            self.assertEqual(lab.sha256_file(generation / "report.json"), manifest["report_json_sha256"])
            self.assertEqual(lab.sha256_file(generation / "report.xlsx"), manifest["report_xlsx_sha256"])
        finally:
            lab.rows_for_export = original

    def test_inventory_rerun_preserves_unprocessed_tail(self):
        tracks = []
        for tid in (20, 21):
            lev, vrm = lab.cache_paths(self.root, tid, "1.0.0", tid * 10, tid * 10 + 1)
            lev.parent.mkdir(parents=True, exist_ok=True); make_lev(lev)
            vrm.write_bytes(b"\0" * 16 + b"\1\0\1\0\0\0")
            def media(path, mid, role):
                return {"id": mid, "type": role, "version": "1.0.0", "modes": ["time_trial"],
                        "file_size": path.stat().st_size, "crc32_hash": lab.file_crc32(path),
                        "download_url": f"https://www.projectsaphi.com/media/{mid}"}
            tracks.append({"id": tid, "name": f"Track {tid}", "author": "Tester", "collection": "saphi",
                "type": "race", "versions": [{"version": "1.0.0", "lev": media(lev, tid * 10, "lev"),
                "vrm": media(vrm, tid * 10 + 1, "vrm"), "saphi_modes": ["time_trial"],
                "consistent": True, "inconsistency": ""}]})
        catalogue = {"schema_version": 2, "tracks": tracks}
        lab.atomic_json(self.root / "catalogue.json", catalogue)
        args = argparse.Namespace(root=self.root, verifier=self.verifier, offline=True)
        lab.command_analyze(args)
        original = lab.inventory(self.root)
        self.assertEqual(len(original["entries"]), 2)

        catalogue["tracks"][0]["name"] = "Track 20 revised"
        lab.atomic_json(self.root / "catalogue.json", catalogue)
        fired = False
        def interrupt(stage, target):
            nonlocal fired
            if not fired and target == self.root / "inventory.json" and stage == "after_directory_sync":
                fired = True; raise RuntimeError("interrupt after first committed entry")
        lab._fault_hook = interrupt
        try:
            with self.assertRaisesRegex(RuntimeError, "interrupt"):
                lab.command_analyze(args)
        finally:
            lab._fault_hook = None
        partial = lab.inventory(self.root)
        self.assertEqual(len(partial["entries"]), 2)
        self.assertEqual({row["track_id"] for row in partial["entries"]}, {20, 21})
        lab.command_analyze(args)
        finished = lab.inventory(self.root)
        self.assertEqual(len(finished["entries"]), 2)
        self.assertEqual(next(row for row in finished["entries"] if row["track_id"] == 20)["name"], "Track 20 revised")


if __name__ == "__main__": unittest.main()
