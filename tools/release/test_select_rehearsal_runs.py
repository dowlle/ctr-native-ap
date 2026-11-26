"""Companion selection must use current successful artifacts or fail closed."""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from textwrap import dedent
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "select_rehearsal_runs", Path(__file__).with_name("select-rehearsal-runs.py"))
selector = importlib.util.module_from_spec(spec)
spec.loader.exec_module(selector)


class CompanionSelectionTests(unittest.TestCase):
    def select(self, response, code=0, candidates=None, required=False,
               *, allow_pull_request=False, missing_artifact_ids=(), lookup_error_ids=()):
        self.query = None
        def runs(repo, workflow, query):
            self.query = query
            return iter(candidates if candidates is not None else [
                dict(id=1, head_branch="pair/topic", head_sha="current",
                     event="workflow_dispatch", conclusion="success")])
        def has_artifacts(repo, run_id, names):
            if run_id in lookup_error_ids:
                raise subprocess.CalledProcessError(1, "gh")
            return run_id not in missing_artifact_ids

        with patch.object(selector.subprocess, "run", return_value=subprocess.CompletedProcess(
                [], code, json.dumps(response), "")), patch.object(selector, "runs", side_effect=runs), \
                patch.object(selector, "has_artifacts", side_effect=has_artifacts):
            return selector.choose_apworld(
                "owner/world", "main", "pair/topic", required,
                allow_pull_request=allow_pull_request)

    def test_matching_branch_uses_current_success(self):
        self.assertEqual(self.select({"object": {"sha": "current"}}), ([1], "current"))
        self.assertIn("branch=pair%2Ftopic", self.query)

    def test_stale_success_does_not_satisfy_companion(self):
        with self.assertRaisesRegex(RuntimeError, "no successful"):
            self.select({"object": {"sha": "newer"}})

    def test_pr_rehearsal_accepts_success_on_current_companion_head(self):
        current = dict(id=1, head_branch="pair/topic", head_sha="current",
                       event="pull_request", conclusion="success",
                       head_repository={"full_name": "owner/world"})
        candidates = [
            dict(current, id=6, event="workflow_dispatch"),
            dict(current, id=8, event="push"),
            dict(current, id=2, head_sha="stale"),
            dict(current, id=3, head_branch="elsewhere"),
            dict(current, id=4, conclusion="failure"),
            dict(current, id=5),
            dict(current, id=7, head_repository={"full_name": "fork/world"}),
            current,
        ]
        self.assertEqual(self.select({"object": {"sha": "current"}}, candidates=candidates,
                                     allow_pull_request=True, missing_artifact_ids=(5,)), ([1, 6, 8], "current"))
        self.assertIn("head_sha=current", self.query)
        self.assertEqual(self.select({"object": {"sha": "current"}}, candidates=candidates[:-1],
                                     allow_pull_request=True, missing_artifact_ids=(5,)), ([6, 8], "current"))
        with self.assertRaisesRegex(RuntimeError, "no successful"):
            self.select({"object": {"sha": "current"}}, candidates=candidates,
                        allow_pull_request=True, missing_artifact_ids=(1, 5, 6, 8))

    def test_manual_rehearsal_rejects_pr_run(self):
        pr = dict(id=1, head_branch="pair/topic", head_sha="current",
                  event="pull_request", conclusion="success",
                  head_repository={"full_name": "owner/world"})
        with self.assertRaisesRegex(RuntimeError, "no successful"):
            self.select({"object": {"sha": "current"}}, candidates=[pr], required=True)

    def test_pr_rehearsal_keeps_healthy_runs_after_an_artifact_lookup_error(self):
        first = dict(id=1, head_branch="pair/topic", head_sha="current",
                     event="pull_request", conclusion="success",
                     head_repository={"full_name": "owner/world"})
        candidates = [first, dict(first, id=2), dict(first, id=3, event="push")]
        with patch("sys.stderr") as stderr:
            self.assertEqual(
                self.select({"object": {"sha": "current"}}, candidates=candidates,
                            allow_pull_request=True, lookup_error_ids=(2,)), ([1, 3], "current"))
        stderr.write.assert_any_call("Could not look up ctr.apworld for run 2")

    def test_missing_companion_uses_base(self):
        base = dict(id=2, head_branch="main", head_sha="base", event="push", conclusion="success")
        pr = dict(base, id=3, event="pull_request")
        self.assertEqual(self.select({"status": "404"}, 1, [pr, base],
                                     allow_pull_request=True), ([2], "base"))
        self.assertIn("branch=main", self.query)

    def test_api_failure_does_not_use_base(self):
        with self.assertRaisesRegex(RuntimeError, "could not resolve"):
            self.select({"status": "403"}, 1)

    def test_explicit_manual_branch_uses_current_head(self):
        self.assertEqual(self.select({"object": {"sha": "current"}}, required=True), ([1], "current"))

    def test_explicit_manual_branch_rejects_stale_success(self):
        with self.assertRaisesRegex(RuntimeError, "no successful"):
            self.select({"object": {"sha": "newer"}}, required=True)

    def test_missing_explicit_manual_branch_does_not_use_base(self):
        with self.assertRaisesRegex(RuntimeError, "could not resolve"):
            self.select({"status": "404"}, 1, required=True)

    def test_missing_artifacts_does_not_satisfy_companion(self):
        with patch.object(selector, "runs", return_value=[dict(
                id=1, head_branch="pair/topic", head_sha="current",
                event="workflow_dispatch", conclusion="success")]), \
                patch.object(selector, "has_artifacts", return_value=False):
            with self.assertRaisesRegex(RuntimeError, "no successful"):
                selector.choose_apworld("owner/world", "pair/topic")


@unittest.skipUnless(shutil.which("bash"), "release assembly uses Bash")
class PackedApworldWorkflowTests(unittest.TestCase):
    def test_merge_tree_mismatch_uses_same_head_fallback_only_when_available(self):
        repo = Path(__file__).resolve().parents[2]
        workflow = (repo / ".github/workflows/release-assembly.yml").read_text(encoding="utf-8")
        step = workflow.split("- name: Download and verify the exact packed apworld\n", 1)[1]
        script = dedent(step.split("run: |\n", 1)[1].split("\n      - name:", 1)[0])
        # Model GitHub's merge-tree artifact, a rerun, and the verifier's exact-source check.
        commands = r"""
git() { printf '%s\n' "$APWORLD_SOURCE_COMMIT"; }
gh() {
  if [[ "$2" == view ]]; then
    case "$3" in
      3) printf 'in_progress  %s\n' "$APWORLD_SOURCE_COMMIT" ;;
      4) printf 'completed success stale\n' ;;
      *) printf 'completed success %s\n' "$APWORLD_SOURCE_COMMIT" ;;
    esac
    return
  fi
  mkdir -p "${9}"
  if [[ "$3" == 1 ]]; then
    printf 'synthetic merge tree' > "${9}/ctr.apworld"
  else
    printf 'companion head' > "${9}/ctr.apworld"
  fi
}
python3() {
  [[ "$4" == "$APWORLD_SOURCE_COMMIT" ]] && [[ "$(cat "$2")" == "companion head" ]]
}
"""
        cases = (("1", "3 4 2", True), ("3", "2", True), ("2", "", True),
                 ("1", "", False), ("3", "", False), ("4", "", False))
        for primary, alternates, accepted in cases:
            with self.subTest(primary=primary, alternates=alternates), tempfile.TemporaryDirectory() as workspace:
                result = subprocess.run(
                    ["bash", "-c", commands + script], cwd=workspace, text=True, capture_output=True,
                    env={**os.environ, "APWORLD_RUN_ID": primary, "APWORLD_ALTERNATE_RUN_IDS": alternates,
                         "APWORLD_SOURCE_COMMIT": "a" * 40, "APWORLD_REPO": "owner/world",
                         "APWORLD_ARTIFACT_NAME": "ctr.apworld"})
                output = Path(workspace) / "apworld-artifact" / "ctr.apworld"
                if primary == "1":
                    self.assertIn("Could not verify packed apworld from run 1", result.stderr)
                for rejected in ("3", "4"):
                    if rejected in (primary, *alternates.split()):
                        self.assertIn(f"Run {rejected} is not successful on apworld commit", result.stderr)
                        self.assertFalse((Path(workspace) / f"apworld-candidate-{rejected}").exists())
                if accepted:
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertIn("verified_apworld_run_id=2", result.stdout)
                    self.assertEqual(output.read_text(encoding="utf-8"), "companion head")
                else:
                    self.assertEqual(result.returncode, 1, result.stderr)
                    self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
