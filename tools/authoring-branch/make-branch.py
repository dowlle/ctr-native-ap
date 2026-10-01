#!/usr/bin/env python3
"""Generate the `authoring-client` branch from a release tag or commit.

The branch is `<ref>` plus exactly one commit that:
  * deletes every Archipelago-only source under ap/ (everything not in KEEP),
  * replaces ap/ap_hooks.h with a stub that only forwards to the authoring
    host (ap/ap_authoring_host.h),
  * makes the authoring client the default CMake build and refuses CTR_AP,
  * reduces tools/ci/build-clients.sh to the vanilla and authoring-client
    variants,
  * marks host harnesses that need a deleted file as skipped, with a reason,
  * adds AUTHORING-CLIENT.md at the repository root.

Nothing else changes. `#ifdef CTR_AP` blocks in shared engine files stay in
place: they compile out. The branch is regenerated per release and never
committed to by hand, and it is never a pull request base (RELEASING.md).

Usage:
  tools/authoring-branch/make-branch.py <tag-or-commit> [--branch NAME] [--replace]

The commit uses the repository's configured git identity. Nothing is pushed.
"""
import argparse
import importlib.util
import os
import re
import shutil
import subprocess
import sys
import tempfile

# ap/ files the authoring client builds from. Everything else under ap/ (except
# ap/vendor, which holds only the dependency pins and fetch script) is
# Archipelago-only and is deleted.
KEEP = {
    # authoring host: the AP-layer services the modules below call
    "ap/ap_authoring_host.c", "ap/ap_authoring_host.h",
    # Box Author Mode
    "ap/ap_author.c", "ap/ap_author.h", "ap/ap_author_custom.h",
    "ap/ap_author_pad.h", "ap/ap_author_ready.h",
    "ap/ap_placement_table.h", "ap/ap_placements_data.h",
    "ap/ap_spawn.c", "ap/ap_spawn.h",
    "ap/ap_marker_model.c", "ap/ap_marker_model.h", "ap/ap_marker_model_data.h",
    "ap/ap_box_model.h", "ap/ap_box_measure.c", "ap/ap_box_spawn_pos.c",
    "ap/ap_box_offset_logic.h",
    # AI lap recorder
    "ap/ap_navrec.c", "ap/ap_navrec.h", "ap/ap_navrec_format.h",
    "ap/ap_navrec_identity_logic.h", "ap/ap_navrec_label_logic.h",
    "ap/ap_navrec_lane_logic.h",
    # custom tracks: Saphi downloader and the instance pool rule
    "ap/ap_custom_track_download.cpp", "ap/ap_custom_track_download.h",
    "ap/ap_instance_pool_logic.h",
    # version stamp written into placement files and recordings
    "ap/ap_version.h",
    # replaced by a stub below
    "ap/ap_hooks.h",
}

HOOKS_STUB = """#ifndef AP_HOOKS_H
#define AP_HOOKS_H

// authoring-client branch: the Archipelago layer is not on this branch. The
// authoring modules include this header for the few AP-layer services they
// call, which the authoring host provides (ap_authoring_host.h). On main this
// file is the full Archipelago hook header. CMakeLists.txt refuses CTR_AP here.

#include "ap_authoring_host.h"

#endif // AP_HOOKS_H
"""

CMAKE_GUARD = """
# authoring-client branch (tools/authoring-branch/make-branch.py): the
# Archipelago sources are not here, so the AP build cannot be configured.
if(CTR_AP)
    message(FATAL_ERROR "The authoring-client branch has no Archipelago code. "
        "Build the authoring client (the default) or -DCTR_AUTHORING_CLIENT=OFF for vanilla.")
endif()
"""

SKIP_REASON = "needs Archipelago sources that are not on the authoring-client branch"

SEARCH = ("ap", ".", "include")  # the harness runner's -I directories


def load_runner(tree):
    spec = importlib.util.spec_from_file_location(
        "run_harnesses", os.path.join(tree, "tools", "ci", "run-harnesses.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def resolve(tree, including, name, deleted):
    candidates = [os.path.normpath(os.path.join(base, name))
                  for base in (os.path.dirname(including),) + SEARCH]
    for candidate in candidates:
        if os.path.isfile(os.path.join(tree, candidate)):
            return candidate
    for candidate in candidates:
        if candidate in deleted:
            return candidate
    return None


AP_ONLY = re.compile(r"^(ifdef\s+CTR_AP\b|if\s+defined\s*\(?\s*CTR_AP\b(?![^|]*\|\|))")


def includes_deleted(tree, path, deleted, seen, defines_ap):
    """Follow quoted includes from path. Without CTR_AP, an include inside an
    #ifdef CTR_AP region (not its #else) is not followed."""
    if path in deleted:
        return True
    if path in seen or not os.path.isfile(os.path.join(tree, path)):
        return False
    seen.add(path)
    stack = []
    with open(os.path.join(tree, path), encoding="utf-8", errors="replace") as f:
        for line in f:
            directive = re.match(r"\s*#\s*(\w+)\s*(.*)", line)
            if not directive:
                continue
            word, rest = directive.group(1), directive.group(2).strip()
            if word in ("if", "ifdef", "ifndef"):
                stack.append(bool(AP_ONLY.match(f"{word} {rest}")))
            elif word in ("else", "elif") and stack:
                stack[-1] = False
            elif word == "endif" and stack:
                stack.pop()
            elif word == "include" and (defines_ap or not any(stack)):
                name = re.match(r'"([^"]+)"', rest)
                target = name and resolve(tree, path, name.group(1), deleted)
                if target and includes_deleted(tree, target, deleted, seen, defines_ap):
                    return True
    return False


def harness_needs_deleted(tree, harness, deleted, needles, runner):
    with open(os.path.join(tree, harness), encoding="utf-8", errors="replace") as f:
        text = f.read()
    if harness.endswith(".py"):
        return any(path in text or os.path.basename(path) in text for path in deleted)
    command = runner.header_command(os.path.join(tree, harness))
    if command is None:
        # The runner's default recipe, which links EXTRA_UNITS named in the text.
        if any(needle in text for needle in needles):
            return True
        command = runner.DEFAULT_CPP if harness.endswith(".cpp") else runner.DEFAULT_C
    if any(path in command for path in deleted):
        return True
    defines_ap = re.search(r"(^|\s)-DCTR_AP(\s|$)", command) is not None
    return includes_deleted(tree, harness, deleted, set(), defines_ap)


def git(*args, cwd=None, capture=True):
    result = subprocess.run(["git", *args], cwd=cwd, check=True, text=True,
                            stdout=subprocess.PIPE if capture else None)
    return result.stdout.strip() if capture else ""


def fail(message):
    print(f"make-branch: {message}", file=sys.stderr)
    sys.exit(1)


def replace_once(path, old, new):
    with open(path, encoding="utf-8") as f:
        text = f.read()
    if text.count(old) != 1:
        fail(f"{path}: expected exactly one match for {old!r}")
    with open(path, "w", encoding="utf-8") as f:
        f.write(text.replace(old, new, 1))


def strip_tree(tree, ref, sha):
    tracked = git("ls-files", "ap", cwd=tree).splitlines()
    missing = sorted(KEEP - set(tracked))
    if missing:
        fail(f"{ref} lacks files the authoring client needs (is it older than the "
             f"authoring-client refactor?): {', '.join(missing)}")
    deleted = sorted(p for p in tracked if not p.startswith("ap/vendor/") and p not in KEEP)
    if deleted:
        git("rm", "-q", "--", *deleted, cwd=tree)

    with open(os.path.join(tree, "ap", "ap_hooks.h"), "w", encoding="utf-8") as f:
        f.write(HOOKS_STUB)

    cmake = os.path.join(tree, "CMakeLists.txt")
    replace_once(cmake,
                 'option(CTR_AUTHORING_CLIENT "Authoring client without Archipelago: all authoring features" OFF)',
                 'option(CTR_AUTHORING_CLIENT "Authoring client without Archipelago: all authoring features" ON)'
                 + CMAKE_GUARD)

    build = os.path.join(tree, "tools", "ci", "build-clients.sh")
    replace_once(build, "for variant in ap vanilla authoring authoring-client; do",
                 "for variant in vanilla authoring-client; do # authoring-client branch")

    # Harnesses that need a deleted file cannot build here. Skip them with a
    # reason rather than drop them silently. "Need" means: the file, or a file
    # it includes (followed recursively; without -DCTR_AP, includes inside
    # #ifdef CTR_AP regions are not followed), includes a deleted file; or its
    # build line names one; or it names a production unit the harness runner
    # links by name (EXTRA_UNITS) that was deleted.
    deleted_set = set(deleted)
    runner = load_runner(tree)
    needles = [n for n, unit in runner.EXTRA_UNITS.items() if unit in deleted_set]
    listing = os.path.join(tree, "tools", "ci", "harnesses.txt")
    with open(listing, encoding="utf-8") as f:
        lines = f.read().splitlines()
    out, skipped = [], 0
    for line in lines:
        parts = line.split(None, 1)
        if not parts or line.startswith("#") or (len(parts) > 1 and parts[1].startswith("skip")):
            out.append(line)
            continue
        if harness_needs_deleted(tree, parts[0], deleted_set, needles, runner):
            out.append(f"{parts[0]} skip {SKIP_REASON}")
            skipped += 1
        else:
            out.append(line)
    with open(listing, "w", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")

    template = os.path.join(tree, "tools", "authoring-branch", "AUTHORING-CLIENT.md")
    with open(template, encoding="utf-8") as f:
        readme = f.read().replace("{{SOURCE_REF}}", ref).replace("{{SOURCE_SHA}}", sha)
    with open(os.path.join(tree, "AUTHORING-CLIENT.md"), "w", encoding="utf-8") as f:
        f.write(readme)
    git("add", "-A", cwd=tree)
    return len(deleted), skipped


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("ref", help="release tag or commit to generate the branch from")
    parser.add_argument("--branch", default="authoring-client")
    parser.add_argument("--replace", action="store_true",
                        help="delete an existing local branch of that name first")
    args = parser.parse_args()

    repo = git("rev-parse", "--show-toplevel")
    sha = git("rev-parse", "--verify", f"{args.ref}^{{commit}}", cwd=repo)
    exists = subprocess.run(["git", "show-ref", "--verify", "--quiet", f"refs/heads/{args.branch}"],
                            cwd=repo).returncode == 0
    if exists and not args.replace:
        fail(f"branch {args.branch} exists; pass --replace to regenerate it")

    # Build on a temporary branch and move it into place only when the commit
    # exists, so a failure never leaves a half-made or missing branch behind.
    temporary = f"{args.branch}-generating-{os.getpid()}"
    tree = tempfile.mkdtemp(prefix="ctr-authoring-branch-")
    os.rmdir(tree)
    try:
        git("worktree", "add", "-q", "-b", temporary, tree, sha, cwd=repo)
        deleted, skipped = strip_tree(tree, args.ref, sha)
        message = (f"Authoring client branch from {args.ref}\n\n"
                   f"Generated by tools/authoring-branch/make-branch.py from {sha}.\n"
                   f"Removes {deleted} Archipelago-only files under ap/, makes the\n"
                   f"authoring client the default build, and skips {skipped} host\n"
                   f"harnesses that need the removed files. Do not commit to this\n"
                   f"branch by hand; regenerate it from the next release tag.\n")
        git("commit", "-q", "-m", message, cwd=tree)
        head = git("rev-parse", "HEAD", cwd=tree)
        git("worktree", "remove", "--force", tree, cwd=repo)
        if exists:
            git("branch", "-D", args.branch, cwd=repo)
        git("branch", "-m", temporary, args.branch, cwd=repo)
    finally:
        if os.path.isdir(tree):
            git("worktree", "remove", "--force", tree, cwd=repo)
        if subprocess.run(["git", "show-ref", "--verify", "--quiet", f"refs/heads/{temporary}"],
                          cwd=repo).returncode == 0:
            git("branch", "-D", temporary, cwd=repo)
    print(f"{args.branch} = {head} ({args.ref} {sha[:12]} plus one commit: "
          f"{deleted} files removed, {skipped} harnesses skipped). Not pushed.")


if __name__ == "__main__":
    main()
