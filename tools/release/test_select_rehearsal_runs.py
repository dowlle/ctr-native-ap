"""Companion selection must use current successful artifacts or fail closed."""
import importlib.util
import json
from pathlib import Path
import subprocess
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "select_rehearsal_runs", Path(__file__).with_name("select-rehearsal-runs.py"))
selector = importlib.util.module_from_spec(spec)
spec.loader.exec_module(selector)


class CompanionSelectionTests(unittest.TestCase):
    def select(self, response, code=0, candidates=None, required=False):
        self.query = None
        def runs(repo, workflow, query):
            self.query = query
            return iter(candidates if candidates is not None else [
                dict(id=1, head_branch="pair/topic", head_sha="current",
                     event="workflow_dispatch", conclusion="success")])
        with patch.object(selector.subprocess, "run", return_value=subprocess.CompletedProcess(
                [], code, json.dumps(response), "")), patch.object(selector, "runs", side_effect=runs), \
                patch.object(selector, "has_artifacts", return_value=True):
            return selector.choose_apworld("owner/world", "main", "pair/topic", required)

    def test_matching_branch_uses_current_success(self):
        self.assertEqual(self.select({"object": {"sha": "current"}}), (1, "current"))
        self.assertIn("branch=pair%2Ftopic", self.query)

    def test_stale_success_does_not_satisfy_companion(self):
        with self.assertRaisesRegex(RuntimeError, "no successful"):
            self.select({"object": {"sha": "newer"}})

    def test_missing_companion_uses_base(self):
        base = dict(id=2, head_branch="main", head_sha="base", event="push", conclusion="success")
        self.assertEqual(self.select({"status": "404"}, 1, [base]), (2, "base"))
        self.assertIn("branch=main", self.query)

    def test_api_failure_does_not_use_base(self):
        with self.assertRaisesRegex(RuntimeError, "could not resolve"):
            self.select({"status": "403"}, 1)

    def test_explicit_manual_branch_uses_current_head(self):
        self.assertEqual(self.select({"object": {"sha": "current"}}, required=True), (1, "current"))

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


if __name__ == "__main__":
    unittest.main()
