from __future__ import annotations

from pathlib import Path
from types import SimpleNamespace
from contextlib import redirect_stdout
import io
import tempfile
import unittest

from levtool.project import new_project, validate_project
from tests.lev_fixture import build_minimal_lev
from levtool.cli import main as levtool_main
from tools.run_editor_project import (EDITOR_RELOAD_EXIT, EDITOR_ROLLBACK_EXIT, build_command,
                                      generation_output_path, run_editor_loop)


class EditorLauncherTests(unittest.TestCase):
    def test_build_command_can_select_validated_derived_lev_and_hot_reload(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            lev = root / "track.lev"
            vrm = root / "track.vrm"
            project_path = root / "track.editor.json"
            binary = root / "ctr_native_editor"
            lev.write_bytes(build_minimal_lev())
            vrm.write_bytes(b"fixture-vrm")
            project = new_project(lev, vrm, 3)
            derived = root / "track-authored-g0001.lev"
            digest = "a" * 64
            command = build_command(binary, project_path, project, (derived, digest), hot_reload_enabled=True)
            self.assertEqual(command[command.index("--editor-lev") + 1], str(derived))
            self.assertEqual(command[command.index("--editor-lev-sha256") + 1], digest)
            self.assertEqual(command[command.index("--editor-source-lev") + 1], str(lev.resolve()))
            self.assertEqual(command[command.index("--editor-source-lev-sha256") + 1], project["sources"]["lev"]["sha256"])
            self.assertIn("--editor-hot-reload", command)

    def test_legacy_export_hash_without_path_remains_valid(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            lev = root / "track.lev"
            vrm = root / "track.vrm"
            lev.write_bytes(build_minimal_lev())
            vrm.write_bytes(b"fixture-vrm")
            project = new_project(lev, vrm, 3)
            project.pop("last_clean_export_path")
            project["last_clean_export_sha256"] = "b" * 64
            self.assertEqual(validate_project(project), [])

    def test_generation_output_never_selects_an_existing_artifact(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory) / "track-authored.lev"
            first = generation_output_path(base, 7)
            self.assertEqual(first.name, "track-authored-g0007.lev")
            first.write_bytes(b"first")
            second = generation_output_path(base, 7)
            self.assertEqual(second.name, "track-authored-g0007-r2.lev")

    def test_reload_then_rollback_relaunches_validated_generation_and_source(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            lev = root / "track.lev"; vrm = root / "track.vrm"; project_path = root / "track.editor.json"
            lev.write_bytes(build_minimal_lev()); vrm.write_bytes(b"fixture-vrm")
            project = new_project(lev, vrm, 3); project["history"] = {"generation": 1, "clean_generation": 0}
            project_path.write_text(__import__("json").dumps(project), encoding="utf-8")
            run_codes = iter((EDITOR_RELOAD_EXIT, EDITOR_ROLLBACK_EXIT, 0))
            commands: list[list[str]] = []

            def fake_run(command: list[str], **_: object) -> SimpleNamespace:
                commands.append(command)
                return SimpleNamespace(returncode=next(run_codes))

            def real_export(command: list[str], **_: object) -> SimpleNamespace:
                with redirect_stdout(io.StringIO()):
                    result = levtool_main(command[3:])
                return SimpleNamespace(returncode=result)

            result = run_editor_loop(root / "editor", project_path, root / "authored.lev", fake_run, real_export)
            self.assertEqual(result, 0)
            self.assertEqual(len(commands), 3)
            source_path = str(lev.resolve())
            derived_path = str((root / "authored-g0001.lev").resolve())
            self.assertEqual(commands[0][commands[0].index("--editor-lev") + 1], source_path)
            self.assertEqual(commands[1][commands[1].index("--editor-lev") + 1], derived_path)
            self.assertEqual(commands[1][commands[1].index("--editor-source-lev") + 1], source_path)
            self.assertEqual(commands[2][commands[2].index("--editor-lev") + 1], source_path)


if __name__ == "__main__":
    unittest.main()
