#!/usr/bin/env python3
"""Build and run the host harnesses under tools/test-*.c, tools/test-*.cpp and tools/test-*.py.

Each harness is a single translation unit compiled against the freestanding
_logic.h headers. Most carry their own build line in the leading comment block
(a line starting with cc, gcc or g++, possibly continued on following comment
lines). This runner uses that line when present and a default recipe otherwise.

Python harnesses (tools/test-*.py) are run directly with python3 from the repo root.
A line `cmd <name> :: <shell command>` in the list runs a non-file step (for example
a unittest discover) from the repo root. CI's ci.yml runs this script for all of
them, so the local run and CI use one list and cannot drift.

Which harnesses CI enforces is decided by tools/ci/harnesses.txt: one harness
path per line, optionally followed by `skip <reason>`. A harness file that is
neither listed nor skipped fails the run, so adding a harness means deciding
whether CI runs it.

Usage:
  tools/ci/run-harnesses.py            run the listed harnesses, fail on any failure
  tools/ci/run-harnesses.py --all      try every harness, report, never fail (discovery)
  tools/ci/run-harnesses.py --write-list  discovery, then rewrite harnesses.txt from the result

Sharding (CI runs the list as parallel matrix jobs):
  tools/ci/run-harnesses.py --shard K/N --results DIR   run only shard K of N (K from 0), write DIR/shard-K.txt
  tools/ci/run-harnesses.py --summary DIR [--count N]   read every shard file, check the whole list, print the totals

Partition: a harness or `cmd` step named in tools/ci/harness-weights.txt (the slow ones, with measured
seconds) is placed longest first on the least loaded shard; every other name goes to shard sha1(name) % N.
The result depends only on the list and the weights file, so every shard and the summary agree.
The summary redoes the unlisted check over the whole tree and fails when a shard file or a listed
entry is missing, so a shard that never ran cannot pass silently.
"""
import glob, hashlib, os, re, shlex, subprocess, sys, time

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


def run_py(src):
    cmd = "python3 " + src
    try:
        r = subprocess.run(["python3", src], cwd=ROOT, capture_output=True, text=True, timeout=600)
    except Exception as e:  # noqa: BLE001
        return "run-error", cmd, str(e)
    if r.returncode != 0:
        return "run-fail", cmd, (r.stdout + r.stderr)[-1500:]
    return "pass", cmd, ""


def run_cmd(cmd):
    try:
        r = subprocess.run(cmd, shell=True, cwd=ROOT, capture_output=True, text=True, timeout=1200)
    except Exception as e:  # noqa: BLE001
        return "run-error", cmd, str(e)
    if r.returncode != 0:
        return "run-fail", cmd, (r.stdout + r.stderr)[-1500:]
    return "pass", cmd, ""


def run_one(src):
    if src.endswith(".py"):
        return run_py(src)
    cmd, out = build_command(src)
    t0 = time.time()
    try:
        b = subprocess.run(shlex.split(cmd), cwd=ROOT, capture_output=True, text=True, timeout=600)
    except Exception as e:  # noqa: BLE001
        return "build-error", cmd, str(e)
    if b.returncode != 0:
        return "build-fail", cmd, (b.stderr or b.stdout)[-1500:]
    built = time.time() - t0
    try:
        r = subprocess.run([out], cwd=ROOT, capture_output=True, text=True, timeout=600)
    except Exception as e:  # noqa: BLE001
        return "run-error", cmd, str(e)
    if r.returncode != 0:
        return "run-fail", cmd, (r.stdout + r.stderr)[-1500:]
    return "pass", cmd, f"build {built:.1f}s run {time.time() - t0 - built:.1f}s"


def read_list():
    listed, skipped, cmds = {}, {}, {}
    if not os.path.exists(LIST):
        return listed, skipped, cmds
    for ln in open(LIST):
        ln = ln.strip()
        if not ln or ln.startswith("#"):
            continue
        if ln.startswith("cmd "):
            name, _, command = ln[4:].partition("::")
            cmds[name.strip()] = command.strip()
            continue
        parts = ln.split(None, 2)
        if len(parts) >= 2 and parts[1] == "skip":
            skipped[parts[0]] = parts[2] if len(parts) > 2 else ""
        else:
            listed[parts[0]] = True
    return listed, skipped, cmds


WEIGHTS = os.path.join(ROOT, "tools", "ci", "harness-weights.txt")
DEFAULT_WEIGHT = 0.7


def read_weights():
    weights = {}
    if os.path.exists(WEIGHTS):
        for ln in open(WEIGHTS):
            ln = ln.strip()
            if ln and not ln.startswith("#"):
                name, _, secs = ln.rpartition("::")
                weights[name.strip()] = float(secs)
    return weights


def shard_of(name, count, _cache={}):
    key = count
    if key not in _cache:
        listed, _skipped, cmds = read_list()
        names = [n for n in discover() if n in listed] + list(cmds)
        weights = read_weights()
        plan, load = {}, [0.0] * count
        for n in names:
            if n not in weights:
                plan[n] = int(hashlib.sha1(n.encode()).hexdigest(), 16) % count
                load[plan[n]] += DEFAULT_WEIGHT
        for n in sorted((n for n in names if n in weights), key=lambda n: (-weights[n], n)):
            plan[n] = min(range(count), key=lambda i: (load[i], i))
            load[plan[n]] += weights[n]
        _cache[key] = plan
    return _cache[key].get(name, int(hashlib.sha1(name.encode()).hexdigest(), 16) % count)


def flag_value(flag):
    i = sys.argv.index(flag)
    if i + 1 >= len(sys.argv):
        sys.exit(f"{flag} needs a value")
    return sys.argv[i + 1]


def discover():
    all_src = sorted(glob.glob(os.path.join(ROOT, "tools", "test-*.c")) +
                     glob.glob(os.path.join(ROOT, "tools", "test-*.cpp")) +
                     glob.glob(os.path.join(ROOT, "tools", "test-*.py")))
    return [os.path.relpath(p, ROOT) for p in all_src]


def summarize(results_dir):
    """Global verdict for sharded CI: every shard file present, nothing failed, nothing unlisted."""
    listed, skipped, cmds = read_list()
    all_rel = discover()
    files = sorted(glob.glob(os.path.join(results_dir, "**", "shard-*.txt"), recursive=True))
    passed, failed, seen, shards = 0, [], set(), set()
    for f in files:
        shards.add(int(re.search(r"shard-(\d+)\.txt$", f).group(1)))
        for ln in open(f):
            status, name, _secs = ln.rstrip("\n").split("\t")
            seen.add(name)
            if status == "pass":
                passed += 1
            else:
                failed.append(name)
    problems = []
    if "--count" in sys.argv and shards != set(range(int(flag_value("--count")))):
        problems.append(f"expected shards 0..{int(flag_value('--count')) - 1}, found {sorted(shards)}")
    for name in sorted(p for p in listed if p not in all_rel):
        problems.append(f"listed but missing from tools/: {name}")
    for name in sorted(({p for p in all_rel if p in listed} | set(cmds)) - seen):
        problems.append(f"not run by any shard: {name}")
    unlisted = [r for r in all_rel if r not in listed and r not in skipped]
    for r in unlisted:
        print(f"unlisted    {r}  (add it to tools/ci/harnesses.txt, or `skip <reason>`)")
    for r in failed:
        print(f"failed      {r}")
    for pr in problems:
        print(pr)
    print(f"\n{passed} passed, {len(failed)} failed, {len(skipped)} skipped, {len(unlisted)} unlisted")
    return 1 if (failed or unlisted or problems) else 0


def main():
    os.makedirs(OUT, exist_ok=True)
    if "--summary" in sys.argv:
        return summarize(flag_value("--summary"))
    shard = None
    if "--shard" in sys.argv:
        k, _, n = flag_value("--shard").partition("/")
        shard = (int(k), int(n))
        results_dir = flag_value("--results")
        os.makedirs(results_dir, exist_ok=True)
    mode = "ci" if len(sys.argv) < 2 or sys.argv[1] == "--shard" else sys.argv[1]
    all_rel = discover()
    listed, skipped, cmds = read_list()
    results, times = {}, {}
    mine = (lambda name: True) if shard is None else (lambda name: shard_of(name, shard[1]) == shard[0])
    targets = all_rel if mode in ("--all", "--write-list") else [p for p in all_rel if p in listed and mine(p)]
    for rel in targets:
        t0 = time.time()
        status, cmd, detail = run_one(rel)
        times[rel] = time.time() - t0
        results[rel] = (status, cmd, detail)
        print(f"{status:11s} {times[rel]:6.1f}s {rel}" + (f"  ({detail})" if status == "pass" and detail else ""))
        if status != "pass" and mode == "ci":
            print("  cmd: " + cmd)
            print("  " + detail.replace("\n", "\n  "))
    if mode == "ci":
        for name, command in cmds.items():
            if not mine(name):
                continue
            t0 = time.time()
            status, cmd, detail = run_cmd(command)
            times[name] = time.time() - t0
            results[name] = (status, cmd, detail)
            print(f"{status:11s} {times[name]:6.1f}s {name}")
            if status != "pass":
                print("  cmd: " + cmd)
                print("  " + detail.replace("\n", "\n  "))
    if mode == "--write-list":
        with open(LIST, "w") as f:
            f.write("# Harnesses CI runs (tools/ci/run-harnesses.py). One per line; `skip <reason>` opts out.\n")
            f.write("# Regenerate with: tools/ci/run-harnesses.py --write-list\n")
            for name, command in cmds.items():
                f.write(f"cmd {name} :: {command}\n")
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
    if shard is not None:
        # The summary job owns the global unlisted check and the totals.
        with open(os.path.join(results_dir, f"shard-{shard[0]}.txt"), "w") as f:
            for name, v in results.items():
                f.write(f"{v[0]}\t{name}\t{times[name]:.1f}\n")
        print(f"\nshard {shard[0]}/{shard[1]}: {len(results) - len(failed)} passed, {len(failed)} failed")
        return 1 if failed else 0
    unlisted = [r for r in all_rel if r not in listed and r not in skipped]
    for r in unlisted:
        print(f"unlisted    {r}  (add it to tools/ci/harnesses.txt, or `skip <reason>`)")
    print(f"\n{len(results) - len(failed)} passed, {len(failed)} failed, {len(skipped)} skipped, {len(unlisted)} unlisted")
    return 1 if (failed or unlisted) else 0


if __name__ == "__main__":
    sys.exit(main())
