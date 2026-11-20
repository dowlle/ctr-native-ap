"""The client identifies itself honestly to Project Saphi (A6-52).

Every request the download transport sends (Project Saphi, the track registry
and the consent list, all through https_get_host in
ap/ap_custom_track_download.cpp) carries one User-Agent built by
ap_client_user_agent() in ap/ap_client_identity.h. An AP build sends the
client's real name and the same CTR_AP_VERSION the title and the boot log
show; the authoring client (no CTR_AP) sends its engine build identity. Both
carry the public repository link in the usual Name/version (+url) form.

Cases:
  1. the compiled helper returns exactly CTR-AP/<CTR_AP_VERSION>
     (+https://github.com/dowlle/ctr-native-ap) for the version in
     ap/ap_version.h, and CTR-Native-Authoring/<CTR_NATIVE_VERSION> for an
     authoring build, and nothing else,
  2. the old fixed text CTR-AP-alpha7 is gone from the client source,
     including split across adjacent string literals (the scan is for the
     version part alpha7, which no legitimate file carries),
  3. the wire value comes only from the helper: the single WinHttpOpen call
     takes exactly the widened helper result, the single User-Agent header in
     the portable request is exactly the helper concatenation, so a mutation
     that puts the old or any other value on the wire fails here.
"""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]

version_header = (root / "ap/ap_version.h").read_text(encoding="utf-8")
version = re.search(r'#define CTR_AP_VERSION "([^"]+)"', version_header).group(1)
expected_ap = f"CTR-AP/{version} (+https://github.com/dowlle/ctr-native-ap)"
expected_authoring = "CTR-Native-Authoring/9.9.9-harness (+https://github.com/dowlle/ctr-native-ap)"

probe = r'''
#include "ap_client_identity.h"
#include <cstdio>
int main() { std::printf("%s", ap_client_user_agent().c_str()); return 0; }
'''

checks = failures = 0

def expect(ok, what):
    global checks, failures
    checks += 1
    if not ok:
        failures += 1
        print(f"FAIL: {what}")

def compile_probe(defines):
    with tempfile.TemporaryDirectory() as tmp:
        src, exe = Path(tmp) / "ua.cpp", Path(tmp) / "ua"
        src.write_text(probe, encoding="utf-8")
        subprocess.run(["g++", "-m32", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        *defines, "-I", str(root / "ap"), str(src), "-o", str(exe)],
                       check=True, cwd=root)
        return subprocess.run([str(exe)], check=True, capture_output=True, text=True, cwd=root).stdout

# 1. The compiled helper says the client's real name and version, per build.
agent = compile_probe(["-DCTR_AP"])
expect(agent == expected_ap, f"an AP build sends {expected_ap!r}, got {agent!r}")
agent = compile_probe(['-DCTR_NATIVE_VERSION="9.9.9-harness"'])
expect(agent == expected_authoring, f"the authoring client sends {expected_authoring!r}, got {agent!r}")

# 2. The old fixed text is gone, even split across adjacent string literals.
source_suffixes = {".c", ".h", ".cpp", ".hpp", ".md", ".py", ".txt", ".cmake"}
old_hits = [str(p.relative_to(root)) for tree in ("ap", "platform", "game", "include")
            for p in (root / tree).rglob("*")
            if p.is_file() and (p.suffix in source_suffixes or p.name == "CMakeLists.txt")
            and "alpha7" in p.read_text(encoding="utf-8", errors="ignore")]
expect(not old_hits, f"no alpha7 anywhere in the client source, still in: {old_hits}")

# 3. The wire value comes only from the helper.
transport = (root / "ap/ap_custom_track_download.cpp").read_text(encoding="utf-8")
flat = re.sub(r"\s+", "", transport)
expect(transport.count("WinHttpOpen(") == 1 and
       "widen_ascii(ap_client_user_agent())" in transport and
       "WinHttpOpen(user_agent.c_str()," in flat,
       "the one WinHttpOpen call takes exactly the widened helper result")
expect(transport.count("User-Agent:") == 1,
       "exactly one User-Agent header is built in the transport")
request = ('const std::string request = "GET " + path + " HTTP/1.1\\r\\nHost: " + host + '
           '"\\r\\nUser-Agent: " + ap_client_user_agent() + '
           '"\\r\\nAccept-Encoding: identity\\r\\nConnection: close\\r\\n\\r\\n";')
expect(re.sub(r"\s+", "", request) in flat,
       "the portable request's User-Agent is exactly ap_client_user_agent()")

print(f"{checks} checks, {failures} failures")
if failures:
    raise SystemExit(1)
print("PASS: the download transport identifies the client honestly on every request: "
      "CTR-AP/<title and log version> in an AP build, CTR-Native-Authoring/<engine version> in the "
      "authoring client, both with the repository link; the old fixed text is gone and no other "
      "value can reach the wire")
