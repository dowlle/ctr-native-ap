#!/usr/bin/env python3
"""Build and run the host harnesses under tools/test-*.c and tools/test-*.cpp.

Each harness is a single translation unit compiled against the freestanding
_logic.h headers. Most carry their own build line in the leading comment block
(a line starting with cc, gcc or g++, possibly continued on following comment
lines). This runner uses that line when present and a default recipe otherwise.

Which harnesses CI enforces is decided by tools/ci/harnesses.txt: one harness
path per line, optionally followed by `skip <reason>`. A harness file that is
neither listed nor skipped fails the run, so adding a harness means deciding
whether CI runs it.

Usage:
  tools/ci/run-harnesses.py            run the listed harnesses, fail on any failure
  tools/ci/run-harnesses.py --all      try every harness, report, never fail (discovery)
  tools/ci/run-harnesses.py --write-list  discovery, then rewrite harnesses.txt from the result
"""
import glob, os, re, shlex, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LIST = os.path.join(ROOT, "tools", "ci", "harnesses.txt")
OUT = os.environ.get("HARNESS_OUT", "/tmp/ctr-harness")
JSON_INC = os.environ.get("HARNESS_JSON_INCLUDE", "ap/vendor/json/include")

DEFAULT_C = "cc -Wall -Wextra -DCTR_AP -I ap -I . -I include -o {out} {src} -lm"
# Harnesses without a header build line that compile a production unit name it
# in their text; map the name to the unit to link.
EXTRA_UNITS = {"ap_seedcfg": "ap/ap_seedcfg.cpp", "ap_charseat": "ap/ap_charseat.c"}

DEFAULT_CPP = ("g++ -m32 -std=c++17 -DCTR_AP -I ap -I . -I include -I " + JSON_INC +
               " -o {out} {src}")


def header_command(src):
    """Return the build command from the leading comment, or None."""
    lines = open(src, encoding="utf-8", errors="replace").read().splitlines()[:40]
    cmd = None
    for i, ln in enumerate(lines):
        if not ln.startswith("//"):
            if cmd is not None:
                break
            continue
        body = ln[2:].strip()
        if cmd is None:
            if re.match(r"^(cc|gcc|g\+\+|c\+\+|clang|clang\+\+)\s", body):
                cmd = body
            continue
        # continuation: option-looking line, or a line that names the source file
        if body.startswith("/tmp/") or body.startswith("&&"):
            break  # the run step; we run the binary ourselves
        if (body.startswith("-") or body.startswith("\\") or body.endswith("\\") or "-o /tmp/" in body
                or re.match(r"^(tools|ap|platform|game|include)/", body)):
            cmd += " " + body
        else:
            break
    if cmd is None:
        return None
    cmd = cmd.replace("\\", " ")
    # drop the trailing "&& /tmp/x" run step; we run the binary ourselves
    cmd = cmd.split("&&")[0].strip()
    return cmd


def build_command(src):
    name = os.path.splitext(os.path.basename(src))[0]
    out = os.path.join(OUT, name)
    cmd = header_command(src)
    if cmd:
        # rewrite the header's -o target to our output dir
        cmd = re.sub(r"-o\s+\S+", "-o " + out, cmd)
        if "-o " not in cmd:
            cmd += " -o " + out
        cmd = cmd.replace("ap/vendor/json/include", JSON_INC)
    else:
        tmpl = DEFAULT_CPP if src.endswith(".cpp") else DEFAULT_C
        cmd = tmpl.format(out=out, src=src)
        # production units a harness links against when it names them
        text = open(os.path.join(ROOT, src), encoding="utf-8", errors="replace").read()
        for needle, unit in EXTRA_UNITS.items():
            if needle in text:
                cmd += " " + unit
    return cmd, out


def run_one(src):
    cmd, out = build_command(src)
    try:
        b = subprocess.run(shlex.split(cmd), cwd=ROOT, capture_output=True, text=True, timeout=600)
    except Exception as e:  # noqa: BLE001
        return "build-error", cmd, str(e)
    if b.returncode != 0:
        return "build-fail", cmd, (b.stderr or b.stdout)[-1500:]
    try:
        r = subprocess.run([out], cwd=ROOT, capture_output=True, text=True, timeout=600)
    except Exception as e:  # noqa: BLE001
        return "run-error", cmd, str(e)
    if r.returncode != 0:
        return "run-fail", cmd, (r.stdout + r.stderr)[-1500:]
    return "pass", cmd, ""


def read_list():
    listed, skipped = {}, {}
    if not os.path.exists(LIST):
        return listed, skipped
    for ln in open(LIST):
        ln = ln.strip()
        if not ln or ln.startswith("#"):
            continue
        parts = ln.split(None, 2)
        if len(parts) >= 2 and parts[1] == "skip":
            skipped[parts[0]] = parts[2] if len(parts) > 2 else ""
        else:
            listed[parts[0]] = True
    return listed, skipped


def main():
    os.makedirs(OUT, exist_ok=True)
    mode = sys.argv[1] if len(sys.argv) > 1 else "ci"
    all_src = sorted(glob.glob(os.path.join(ROOT, "tools", "test-*.c")) +
                     glob.glob(os.path.join(ROOT, "tools", "test-*.cpp")))
    all_rel = [os.path.relpath(p, ROOT) for p in all_src]
    listed, skipped = read_list()
    results = {}
    targets = all_rel if mode in ("--all", "--write-list") else [p for p in all_rel if p in listed]
    for rel in targets:
        status, cmd, detail = run_one(rel)
        results[rel] = (status, cmd, detail)
        print(f"{status:11s} {rel}")
        if status != "pass" and mode == "ci":
            print("  cmd: " + cmd)
            print("  " + detail.replace("\n", "\n  "))
    if mode == "--write-list":
        with open(LIST, "w") as f:
            f.write("# Harnesses CI runs (tools/ci/run-harnesses.py). One per line; `skip <reason>` opts out.\n")
            f.write("# Regenerate with: tools/ci/run-harnesses.py --write-list\n")
            for rel in all_rel:
                st = results[rel][0]
                if st == "pass":
                    f.write(rel + "\n")
                else:
                    f.write(f"{rel} skip {st} under the generic recipe; give it a build line in its header comment\n")
        print("wrote " + LIST)
        return 0
    if mode == "--all":
        return 0
    failed = [r for r, v in results.items() if v[0] != "pass"]
    unlisted = [r for r in all_rel if r not in listed and r not in skipped]
    for r in unlisted:
        print(f"unlisted    {r}  (add it to tools/ci/harnesses.txt, or `skip <reason>`)")
    print(f"\n{len(results) - len(failed)} passed, {len(failed)} failed, {len(skipped)} skipped, {len(unlisted)} unlisted")
    return 1 if (failed or unlisted) else 0


if __name__ == "__main__":
    sys.exit(main())
