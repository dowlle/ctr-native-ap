"""The client identifies itself honestly to Project Saphi (A6-52).

Every request the download transport sends (Project Saphi, the track registry
and the consent list, all through https_get_host in
ap/ap_custom_track_download.cpp) carries one User-Agent built by
ap_client_user_agent() in ap/ap_client_identity.h: the client's real name and
the same CTR_AP_VERSION the title and the boot log show, plus the public
repository link, in the usual Name/version (+url) form.

Cases:
  1. the compiled helper returns exactly CTR-AP/<CTR_AP_VERSION>
     (+https://github.com/dowlle/ctr-native-ap) for the version in
     ap/ap_version.h, and nothing else,
  2. the old fixed text CTR-AP-alpha7 appears nowhere in the client source,
  3. both transports (Windows WinHTTP and the portable asio one) build their
     request through the helper, so no request can go out with the old text.
"""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]

version_header = (root / "ap/ap_version.h").read_text(encoding="utf-8")
version = re.search(r'#define CTR_AP_VERSION "([^"]+)"', version_header).group(1)
expected = f"CTR-AP/{version} (+https://github.com/dowlle/ctr-native-ap)"

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

# 1. The compiled helper says the client's real name and version.
with tempfile.TemporaryDirectory() as tmp:
    src, exe = Path(tmp) / "ua.cpp", Path(tmp) / "ua"
    src.write_text(probe, encoding="utf-8")
    subprocess.run(["g++", "-m32", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    "-I", str(root / "ap"), str(src), "-o", str(exe)], check=True, cwd=root)
    agent = subprocess.run([str(exe)], check=True, capture_output=True, text=True, cwd=root).stdout
expect(agent == expected, f"the helper returns {expected!r}, got {agent!r}")

# 2. The old fixed text is gone from the whole client source.
source_suffixes = {".c", ".h", ".cpp", ".hpp", ".md", ".py", ".txt", ".cmake"}
old_hits = [str(p.relative_to(root)) for tree in ("ap", "platform", "game", "include")
            for p in (root / tree).rglob("*")
            if p.is_file() and (p.suffix in source_suffixes or p.name == "CMakeLists.txt")
            and "CTR-AP-alpha7" in p.read_text(encoding="utf-8", errors="ignore")]
expect(not old_hits, f"the old fixed User-Agent is gone, still in: {old_hits}")

# 3. Both transports route their request through the helper.
transport = (root / "ap/ap_custom_track_download.cpp").read_text(encoding="utf-8")
expect("widen_ascii(ap_client_user_agent())" in transport,
       "the Windows WinHTTP session is opened with ap_client_user_agent()")
expect('"\\r\\nUser-Agent: " + ap_client_user_agent()' in transport,
       "the portable transport's request header uses ap_client_user_agent()")

print(f"{checks} checks, {failures} failures")
if failures:
    raise SystemExit(1)
print("PASS: the download transport identifies the client as CTR-AP/<title and log version> "
      "(+https://github.com/dowlle/ctr-native-ap) on every request; the old fixed text is gone")
