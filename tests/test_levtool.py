from __future__ import annotations

import json
import io
from pathlib import Path
import tempfile
import unittest
from contextlib import redirect_stdout

from levtool.cli import main as levtool_main
from levtool.format import LevFile, contiguous_byte_differences
from levtool.inspection import geometry_query, lev_diff, lev_inspection, project_inspection
from levtool.project import content_manifest, new_project, validate_project
from levtool.writer import Placement, add_placement, move_local
from tests.lev_fixture import build_minimal_lev


class LevFileTests(unittest.TestCase):
    def test_generated_fixture_reference_graph(self) -> None:
        lev = LevFile(build_minimal_lev())
        self.assertTrue(lev.valid, lev.diagnostics)
        instance = lev.instances[0]
        self.assertEqual(instance.position, (100, 200, 300))
        self.assertEqual((instance.global_references, instance.pvs_references, instance.bsp_references), (1, 1, 1))
        self.assertEqual(instance.total_references, 3)

    def test_duplicate_pointer_slot_fails(self) -> None:
        raw = bytearray(build_minimal_lev()); map_offset = int.from_bytes(raw[0:4], "little", signed=True) + 4
        num_bytes = int.from_bytes(raw[map_offset:map_offset + 4], "little", signed=True); first = raw[map_offset + 4:map_offset + 8]
        raw[map_offset:map_offset + 4] = (num_bytes + 4).to_bytes(4, "little", signed=True); raw.extend(first)
        self.assertIn("pointer-map-duplicate", {item.code for item in LevFile(bytes(raw)).errors})

    def test_unmapped_pvs_entry_fails(self) -> None:
        raw = bytearray(build_minimal_lev()); lev = LevFile(bytes(raw)); pvs_start = next(iter(lev.pvs_lists))
        map_offset = lev.ptr_map_offset + 4; num_bytes = int.from_bytes(raw[map_offset:map_offset + 4], "little", signed=True); slots_begin = map_offset + 4
        slots = [int.from_bytes(raw[slots_begin + i:slots_begin + i + 4], "little", signed=True) for i in range(0, num_bytes, 4)]
        slots.remove(pvs_start); rebuilt = raw[:map_offset]; rebuilt.extend((len(slots) * 4).to_bytes(4, "little", signed=True))
        for slot in slots: rebuilt.extend(slot.to_bytes(4, "little", signed=True))
        self.assertIn("pvs-instance-list-entry-unmapped", {item.code for item in LevFile(bytes(rebuilt)).errors})

    def test_byte_diff(self) -> None:
        self.assertEqual(list(contiguous_byte_differences(b"abc", b"abc")), [])
        self.assertEqual(list(contiguous_byte_differences(b"abcde", b"abXYe")), [(2, b"cd", b"XY")])

    def test_add_placement_rebuilds_reference_graph(self) -> None:
        source = LevFile(build_minimal_lev())
        raw = add_placement(source, Placement("crate_new", "item-crate", (110, 200, 305), (0, 0x500, 0)))
        derived = LevFile(raw)
        self.assertTrue(derived.valid, derived.diagnostics)
        self.assertEqual(len(derived.instances), 2)
        added = derived.instances[1]
        self.assertEqual(added.name, "crate_new")
        self.assertEqual(added.position, (110, 200, 305))
        self.assertEqual((added.global_references, added.pvs_references, added.bsp_references), (1, 1, 1))

    def test_local_move_preserves_graph_and_size(self) -> None:
        source = LevFile(build_minimal_lev())
        raw = move_local(source, 0, (120, 200, 310), (0, 0x600, 0))
        moved = LevFile(raw)
        self.assertTrue(moved.valid, moved.diagnostics)
        self.assertEqual(len(raw), len(source.raw))
        self.assertEqual(moved.pointer_slots, source.pointer_slots)
        self.assertEqual(moved.instances[0].position, (120, 200, 310))
        self.assertEqual((moved.instances[0].global_references, moved.instances[0].pvs_references,
                          moved.instances[0].bsp_references), (1, 1, 1))

    def test_versioned_inspection_reports_reference_membership(self) -> None:
        report = lev_inspection(LevFile(build_minimal_lev()), include_path=False)
        self.assertEqual((report["schema"], report["version"]), ("ctr-native-lev-inspection", 1))
        self.assertTrue(report["validation"]["valid"])
        self.assertEqual(report["graph"]["reference_counts"], {"global": 1, "pvs": 1, "bsp": 1, "other": 0})
        self.assertEqual((report["geometry"]["vertex_count"], report["geometry"]["quadblock_count"]), (1, 1))
        self.assertEqual(report["geometry"]["quadblocks"][0]["terrain"], 2)
        self.assertEqual(len(report["spawns"]["driver_grid"]), 8)
        self.assertEqual(report["instances"][0]["membership"]["global"], True)
        self.assertEqual(len(report["instances"][0]["membership"]["pvs_lists"]), 1)
        self.assertEqual(len(report["instances"][0]["membership"]["bsp_hitbox_lists"]), 1)

    def test_inspection_is_path_independent_when_requested(self) -> None:
        raw = build_minimal_lev()
        self.assertEqual(lev_inspection(LevFile(raw, "one.lev"), include_path=False),
                         lev_inspection(LevFile(raw, "elsewhere/two.lev"), include_path=False))

    def test_versioned_semantic_diff_reports_instance_move(self) -> None:
        source = LevFile(build_minimal_lev())
        moved = LevFile(move_local(source, 0, (120, 200, 310), (0, 0x600, 0)))
        report = lev_diff(source, moved, include_paths=False)
        self.assertEqual((report["schema"], report["version"]), ("ctr-native-lev-diff", 1))
        self.assertFalse(report["identical"])
        self.assertEqual(report["summary"]["instance_change_count"], 1)
        self.assertEqual(report["instance_changes"][0]["before"]["position"], (100, 200, 300))
        self.assertEqual(report["instance_changes"][0]["after"]["position"], (120, 200, 310))
        self.assertEqual(report["summary"]["quadblock_change_count"], 0)
        self.assertEqual(report["summary"]["pvs_membership_change_count"], 0)
        self.assertEqual(report["summary"]["bsp_membership_change_count"], 1)

    def test_semantic_diff_ignores_relocated_existing_records_on_add(self) -> None:
        source = LevFile(build_minimal_lev())
        derived = LevFile(add_placement(source, Placement("crate_new", "item-crate", (100, 200, 300), (0, 0x400, 0))))
        report = lev_diff(source, derived, include_paths=False)
        self.assertEqual(report["summary"]["instance_change_count"], 1)
        self.assertEqual(report["instance_changes"][0]["index"], 1)
        self.assertEqual(report["summary"]["quadblock_change_count"], 0)
        self.assertEqual(report["summary"]["pvs_membership_change_count"], 1)
        self.assertEqual(report["summary"]["bsp_membership_change_count"], 1)

    def test_geometry_query_reports_quad_pvs_and_bsp_membership(self) -> None:
        report = geometry_query(LevFile(build_minimal_lev()), (100, 200, 300))
        self.assertEqual((report["schema"], report["version"]), ("ctr-native-geometry-query", 1))
        self.assertEqual(report["quadblocks"]["containing_indices"], [0])
        self.assertEqual(report["visibility"]["pvs_instance_indices"], [0])
        self.assertEqual(len(report["collision"]["containing_bsp_leaves"]), 1)
        self.assertEqual(report["nearest_instance"]["index"], 0)


class ProjectTests(unittest.TestCase):
    def test_inspection_schema_documents_are_valid_json(self) -> None:
        root = Path(__file__).resolve().parents[1]
        lev_schema = json.loads((root / "schemas/lev-inspection-v1.schema.json").read_text(encoding="utf-8"))
        project_schema = json.loads((root / "schemas/project-inspection-v1.schema.json").read_text(encoding="utf-8"))
        diff_schema = json.loads((root / "schemas/lev-diff-v1.schema.json").read_text(encoding="utf-8"))
        query_schema = json.loads((root / "schemas/geometry-query-v1.schema.json").read_text(encoding="utf-8"))
        self.assertEqual(lev_schema["properties"]["schema"]["const"], "ctr-native-lev-inspection")
        self.assertEqual(project_schema["properties"]["schema"]["const"], "ctr-native-project-inspection")
        self.assertEqual(diff_schema["properties"]["schema"]["const"], "ctr-native-lev-diff")
        self.assertEqual(query_schema["properties"]["schema"]["const"], "ctr-native-geometry-query")

    def test_project_and_manifest(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); lev_path = root / "track.lev"; vrm_path = root / "track.vrm"
            lev_path.write_bytes(build_minimal_lev()); vrm_path.write_bytes(b"fixture-vrm")
            project = new_project(lev_path, vrm_path, 3)
            project["ap_candidates"] = [{"id": "ap-000", "ordinal": 0, "position": [1, 2, 3], "rotation": [0, 0, 0]}]
            self.assertEqual(validate_project(project), [])
            manifest = content_manifest(project); self.assertEqual(manifest["track"]["host_slot"], 3)
            self.assertFalse(manifest["capabilities"]["vanilla_lev_writeback"]); json.dumps(manifest)
            self.assertIsNone(manifest["derived_lev_path"])

    def test_project_inspection_combines_health_state_and_lev_graph(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); lev_path = root / "track.lev"; vrm_path = root / "track.vrm"
            lev_path.write_bytes(build_minimal_lev()); vrm_path.write_bytes(b"fixture-vrm")
            project = new_project(lev_path, vrm_path, 3)
            project["history"] = {"generation": 2, "clean_generation": 1}
            project["vanilla_objects"] = [{"id": "vanilla-000", "kind": "item-crate",
                                             "position": [1, 2, 3], "rotation": [0, 0, 0]}]
            report = project_inspection(project)
            self.assertEqual((report["schema"], report["version"]), ("ctr-native-project-inspection", 1))
            self.assertTrue(report["project"]["valid"])
            self.assertTrue(report["sources"]["lev"]["matches"])
            self.assertTrue(report["state"]["dirty"])
            self.assertEqual(report["authored_objects"]["vanilla_by_kind"], {"item-crate": 1})
            self.assertEqual(report["source_lev"]["graph"]["instance_count"], 1)

    def test_project_export_records_validated_destination_for_hot_reload(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); lev_path = root / "track.lev"; vrm_path = root / "track.vrm"
            project_path = root / "track.editor.json"; output_path = root / "track-authored-g0001.lev"
            lev_path.write_bytes(build_minimal_lev()); vrm_path.write_bytes(b"fixture-vrm")
            project_path.write_text(json.dumps(new_project(lev_path, vrm_path, 3)), encoding="utf-8")
            with redirect_stdout(io.StringIO()):
                result = levtool_main(["project-export", str(project_path), "--output", str(output_path)])
            self.assertEqual(result, 0)
            updated = json.loads(project_path.read_text(encoding="utf-8"))
            self.assertEqual(updated["last_clean_export_path"], str(output_path.resolve()))
            self.assertTrue(output_path.is_file())
            inspection = project_inspection(updated)
            self.assertEqual(inspection["state"]["last_clean_export_path"], str(output_path.resolve()))
            self.assertTrue(inspection["clean_export"]["matches"])
            output_path.write_bytes(b"tampered")
            self.assertIn("last_clean_export_path hash no longer matches", validate_project(updated))


if __name__ == "__main__":
    unittest.main()
