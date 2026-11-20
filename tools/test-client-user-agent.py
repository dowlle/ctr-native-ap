"""The client identifies itself honestly to Project Saphi (A6-52).

Every request the download transport sends (Project Saphi, the track registry
and the consent list, all through https_get_host in
ap/ap_custom_track_download.cpp) carries one User-Agent built by
ap_client_user_agent() in ap/ap_client_identity.h. An AP build sends the
client's real name and the same CTR_AP_VERSION the title and the boot log
show; the authoring client (no CTR_AP) sends its engine build identity. Both
carry the public repository link in the usual Name/version (+url) form.

Cases:
  1. the compiled helpers return exactly CTR-AP/<CTR_AP_VERSION> and the
     User-Agent: header line for an AP build, and
     CTR-Native-Authoring/<CTR_NATIVE_VERSION> for an authoring build,
  2. the old fixed text CTR-AP-alpha7 appears nowhere in the client source,
  3. the wire value comes only from the helper: the one WinHttpOpen call
     (spelling-tolerant) takes exactly the widened helper result, whatever the
     variable is named; the portable request's User-Agent line is exactly
     ap_client_user_agent_header() and every socket write sends that one
     buffer; a server path containing a line break is refused in both
     transports so no extra header can be injected.
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
int main()
{
	std::printf("%s\n%s", ap_client_user_agent().c_str(), ap_client_user_agent_header().c_str());
	return 0;
}
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
        out = subprocess.run([str(exe)], check=True, capture_output=True, text=True, cwd=root).stdout
        return out.splitlines()

# 1. The compiled helpers say the client's real name and version, per build.
agent, header = compile_probe(["-DCTR_AP"])
expect(agent == expected_ap, f"an AP build sends {expected_ap!r}, got {agent!r}")
expect(header == f"User-Agent: {expected_ap}", f"the header line is User-Agent: {expected_ap!r}, got {header!r}")
agent, header = compile_probe(['-DCTR_NATIVE_VERSION="9.9.9-harness"'])
expect(agent == expected_authoring, f"the authoring client sends {expected_authoring!r}, got {agent!r}")
expect(header == f"User-Agent: {expected_authoring}",
       f"the authoring header line is User-Agent: {expected_authoring!r}, got {header!r}")

# 2. The old fixed text is gone from the whole client source. (Only the
#    contiguous old value: the structural checks below catch attempts to hide
#    it across adjacent literals, and a future alpha7 release stays legal.)
source_suffixes = {".c", ".h", ".cpp", ".hpp", ".md", ".py", ".txt", ".cmake"}
old_hits = [str(p.relative_to(root)) for tree in ("ap", "platform", "game", "include")
            for p in (root / tree).rglob("*")
            if p.is_file() and (p.suffix in source_suffixes or p.name == "CMakeLists.txt")
            and "CTR-AP-alpha7" in p.read_text(encoding="utf-8", errors="ignore")]
expect(not old_hits, f"the old fixed User-Agent is gone, still in: {old_hits}")

# 3. The wire value comes only from the helper.
transport = (root / "ap/ap_custom_track_download.cpp").read_text(encoding="utf-8")
flat = re.sub(r"\s+", "", transport)

winhttp_calls = list(re.finditer(r"WinHttpOpen\s*\(\s*(\w+)\.c_str\(\)\s*,", transport))
other_winhttp = re.sub(r"WinHttpOpen\s*\(\s*\w+\.c_str\(\)\s*,", "", transport)
expect(len(winhttp_calls) == 1 and not re.search(r"WinHttpOpen\s*\(", other_winhttp),
       "exactly one WinHttpOpen call, taking a widened string")
if winhttp_calls:
    var = winhttp_calls[0].group(1)
    expect(re.search(re.escape(var) + r"\s*=\s*widen_ascii\(\s*ap_client_user_agent\(\)\s*\)", transport),
           f"the widened string ({var}) is exactly widen_ascii(ap_client_user_agent())")
else:
    expect(False, "the WinHttpOpen argument variable is identifiable")
expect(transport.count("widen_ascii(ap_client_user_agent())") == 1,
       "the helper is widened exactly once, for the one WinHttpOpen call")

request = ('const std::string request = "GET " + path + " HTTP/1.1\\r\\nHost: " + host + "\\r\\n" + '
           'ap_client_user_agent_header() + '
           '"\\r\\nAccept-Encoding: identity\\r\\nConnection: close\\r\\n\\r\\n";')
expect(re.sub(r"\s+", "", request) in flat,
       "the portable request's User-Agent line is exactly ap_client_user_agent_header()")
expect(transport.count("ap_client_user_agent_header()") == 1,
       "the header helper is used exactly once in the transport")
buffers = set(re.findall(r"(?:async_)?write\s*\(\s*stream\s*,\s*asio::buffer\(\s*(\w+)\s*\)", transport))
expect(buffers == {"request"},
       f"every socket write sends the one checked request buffer, found: {sorted(buffers)}")

# A server path containing a line break is refused in both transports, so a
# malformed listing cannot inject a second User-Agent header.
expect(transport.count("if (!path_safe(path))") == 2 and
       'find_first_of("\\r\\n")' in transport,
       "both transports refuse a path containing a line break")

print(f"{checks} checks, {failures} failures")
if failures:
    raise SystemExit(1)
print("PASS: the download transport identifies the client honestly on every request: "
      "CTR-AP/<title and log version> in an AP build, CTR-Native-Authoring/<engine version> in the "
      "authoring client, both with the repository link; the old fixed text is gone, every socket "
      "write sends the checked buffer, and no server path can inject a header")
