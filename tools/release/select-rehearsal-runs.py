#!/usr/bin/env python3
"""Resolve exact hosted build runs for the shared release assembly workflow."""

from __future__ import annotations

import json
import os
import subprocess
import sys
import time
from pathlib import Path
from urllib.parse import quote, urlencode


def api(path: str) -> dict:
    return json.loads(subprocess.check_output(["gh", "api", path], text=True))


def runs(repo: str, workflow: str, query: str):
    for page in range(1, 11):
        suffix = f"&{query}" if query else ""
        result = api(f"repos/{repo}/actions/workflows/{workflow}/runs?per_page=100&page={page}{suffix}")
        batch = result.get("workflow_runs", [])
        yield from batch
        if len(batch) < 100:
            return


def has_artifacts(repo: str, run_id: int, names: set[str]) -> bool:
    result = api(f"repos/{repo}/actions/runs/{run_id}/artifacts?per_page=100")
    available = {artifact["name"] for artifact in result.get("artifacts", [])
                 if not artifact.get("expired")}
    return names <= available


def choose_native(repo: str, source: str, pr_number: str, pr_head: str) -> int | None:
    event = "pull_request" if pr_number else "push"
    # A dispatch build is also valid for a main/dev source commit.
    query = "event=pull_request" if pr_number else urlencode({"head_sha": source})
    candidates = runs(repo, "build.yml", query)
    for run in candidates:
        if pr_number:
            if run.get("event") != event or run.get("head_sha") not in (source, pr_head):
                continue
            associated = [str(pr.get("number")) for pr in run.get("pull_requests", [])]
            if associated and pr_number not in associated:
                continue
        elif run.get("event") not in ("push", "workflow_dispatch") or run.get("head_sha") != source:
            continue
        if run.get("status") == "completed" and run.get("conclusion") == "success":
            names = {f"clients-{platform}-x86-{source}" for platform in ("windows", "linux")}
            if has_artifacts(repo, run["id"], names):
                return run["id"]
    return None


def choose_apworld(repo: str, branch: str, companion: str = '', required: bool = False) -> tuple[int, str]:
    expected_sha = None
    if companion:
        # Paired PRs can rehearse together before either side merges. Only a
        # missing implicit companion falls back; explicit selections and API
        # errors must fail closed.
        result = subprocess.run(
            ["gh", "api", f"repos/{repo}/git/ref/heads/{quote(companion, safe='')}"],
            text=True, capture_output=True,
        )
        try:
            ref = json.loads(result.stdout)
        except ValueError:
            raise RuntimeError(f"could not resolve companion branch on {repo}")
        if result.returncode:
            if required or ref.get('status') != '404':
                raise RuntimeError(f"could not resolve companion branch on {repo}: {ref}")
        else:
            branch, expected_sha = companion, ref['object']['sha']
    for run in runs(repo, "ctr-world.yml", urlencode({"branch": branch, "status": "success"})):
        if (run.get("event") not in ("push", "workflow_dispatch", "schedule")
                or run.get("head_branch") != branch or run.get("conclusion") != "success"):
            continue
        if expected_sha and run.get('head_sha') != expected_sha:
            continue
        if has_artifacts(repo, run["id"], {"ctr.apworld"}):
            return run["id"], run["head_sha"]
    raise RuntimeError(f"no successful ctr-world run with ctr.apworld on {repo}:{branch}")


def main() -> None:
    mode = os.environ["MODE"]
    source = subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip()
    if mode == "prepare":
        if source != os.environ["NATIVE_SOURCE_COMMIT"]:
            raise RuntimeError("native checkout differs from requested source commit")
        selected = {
            "native_run_id": os.environ["NATIVE_RUN_ID"],
            "native_source_commit": source,
            "apworld_run_id": os.environ["APWORLD_RUN_ID"],
            "apworld_source_commit": os.environ["APWORLD_SOURCE_COMMIT"],
        }
    elif mode == "rehearsal":
        repo = os.environ["GITHUB_REPOSITORY"]
        apworld_repo = os.environ["APWORLD_REPO"]
        world_id, world_sha = choose_apworld(
            apworld_repo, os.environ["APWORLD_BRANCH"],
            os.environ.get("APWORLD_COMPANION_BRANCH", ""),
            os.environ.get("APWORLD_COMPANION_REQUIRED", "false").lower() == "true",
        )
        deadline = time.monotonic() + 55 * 60
        native_id = None
        while native_id is None and time.monotonic() < deadline:
            native_id = choose_native(repo, source, os.environ["PR_NUMBER"], os.environ["PR_HEAD_SHA"])
            if native_id is None:
                print(f"Waiting for successful Build clients artifacts from {source}", flush=True)
                time.sleep(30)
        if native_id is None:
            raise RuntimeError(f"no successful Build clients artifacts from {source} within 55 minutes")
        selected = {
            "native_run_id": str(native_id),
            "native_source_commit": source,
            "apworld_run_id": str(world_id),
            "apworld_source_commit": world_sha,
        }
    else:
        raise RuntimeError(f"invalid assembly mode: {mode}")
    for name, value in selected.items():
        print(f"{name}={value}")
    with Path(os.environ["GITHUB_OUTPUT"]).open("a", encoding="utf-8") as output:
        for name, value in selected.items():
            output.write(f"{name}={value}\n")


if __name__ == "__main__":
    try:
        main()
    except (KeyError, ValueError, OSError, subprocess.CalledProcessError, RuntimeError) as error:
        print(f"release run selection failed: {error}", file=sys.stderr)
        raise SystemExit(1)
