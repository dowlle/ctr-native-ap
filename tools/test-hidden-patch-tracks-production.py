"""Exercise the embedded Play Solo adapter and native needs-laps filter."""

import re
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "ap/ap_solo.cpp").read_text()
header = (root / "include/platform/native_custom_track_visibility.h").read_text()
adapter = (root / "ap/ap_solo_track_visibility.hpp").read_text().split('R"PY(', 1)[1].split(')PY"', 1)[0]
hashes = re.findall(r'"([0-9a-f]{64})"', header)
assert len(set(hashes)) == 5
visible = "60982d262c4a150f0b625495b353795386609217b40837b47bcc395507e66cd1"
assert visible not in hashes

wrapper = r'''
import sys
from types import SimpleNamespace

GAME = 'Crash Team Racing'
HIDDEN, VISIBLE = sys.argv[2:4]

def package(key, digest, name=None):
    return {'id': f'package/{key}/1.0.0', 'content_id': key, 'display_name': name or key,
            'files': [{'role': 'lev', 'sha256': digest}]}

def _section(doc):
    return doc[GAME]

def _world_module(name):
    assert name == 'content_plan'
    return SimpleNamespace(RETAIL={0: 'Crash Cove'},
                           arena_content=SimpleNamespace(ARENAS={0: 'Skull Rock', 1: 'Rocky Road'}))

def _cortex_pick(section):
    return section.get('cortex_vortex_track', 'off') in ('in_pool', 'own_pad')

def _yaml_pick_names(descriptor, descriptors, owners):
    return {descriptor['content_id'], descriptor['display_name'],
            'old-key' if descriptor['content_id'] == 'a' else descriptor['content_id']}

def _descriptor_sources(paths):
    return paths

def apply_tracks(paths, doc, assets, picked, guaranteed, page=False):
    assert not page and picked == guaranteed == []  # no visible rows or player edits
    return doc

def main():
    hidden = package('a', HIDDEN, 'Crash Cove')
    same_label = package('a', VISIBLE, 'Crash Cove')
    keep = package('b', VISIBLE, 'Shared')
    if sys.argv[1] == 'save-room-yaml':
        sources = _descriptor_sources([(hidden, 'room'), (same_label, 'room')])
        selected = next(d for d, _ in sources if d['id'] == same_label['id'])
        assert selected['files'] == same_label['files']
        return 0
    for rows in ([(hidden, 'room')], [(hidden, 'room'), (same_label, 'room')],
                 [(same_label, 'room'), (hidden, 'room')]):
        expected = [(same_label, 'room')] if len(rows) > 1 else []
        assert _descriptor_sources(rows) == expected
    source = {GAME: {'custom_tracks': [hidden, keep], 'required_entries': ['a', 'old-key', 'Crash Cove', 'b'],
                     'track_modes': {'a': ['relic'], 'Crash Cove': ['trophy'], 'b': ['trophy']}}}
    result = apply_tracks([], source, 'assets', [], [])
    assert result[GAME]['custom_tracks'] == [keep]
    assert result[GAME]['required_entries'] == ['Crash Cove', 'b']
    assert result[GAME]['track_modes'] == {'Crash Cove': ['trophy'], 'b': ['trophy']}
    assert source[GAME]['custom_tracks'] == [hidden, keep] and 'a' in source[GAME]['track_modes']
    empty = apply_tracks([], {GAME: {'custom_tracks': [hidden], 'required_entries': ['a']}}, 'assets', [], [])
    assert 'custom_tracks' not in empty[GAME] and empty[GAME]['required_entries'] == []
    named_like_arena = package('arena-name', HIDDEN, 'Skull Rock')
    for arenas, expected in [(['Rocky Road'], []), (['Skull Rock'], ['Skull Rock'])]:
        old = {GAME: {'custom_tracks': [named_like_arena], 'required_entries': ['Skull Rock'],
                      'crystal_arenas': arenas, 'track_modes': {'Skull Rock': ['trophy']}}}
        result = apply_tracks([], old, 'assets', [], [])
        assert result[GAME]['required_entries'] == expected
        assert result[GAME]['track_modes'] == {}
    return 0
'''

def function(signature):
    start = source.index(signature)
    return source[start:source.index("\n}\n", start) + 3]

assert "|| kind == JOB_SAVE_ROOM_YAML" in function("bool JobStart(")

fixture = r'''
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include "ap/ap_solo.h"
#include "ap/ap_solo_logic.hpp"
#include <platform/native_custom_track_visibility.h>
namespace fs = std::filesystem;
using nlohmann::json;
static const size_t kDescriptorMax = 64 * 1024;
static std::string base;
static std::string BaseDir() { return base + "/"; }
static std::string SoloPath(const std::string &rel) { return BaseDir() + "solo/" + rel; }
static const std::string &AssetsDir() { return base; }
'''
for signature in (
    "bool SoloRootIsLink()", "std::string ReadSmallPlainFile(", "bool CopyText(",
    "bool HiddenTrackDescriptor(", "bool PinnedFilePresent(",
    "void AddNeedsLapsRows(std::vector<APSoloTrackInfo> &rows)\n",
):
    fixture += function(signature)
fixture += r'''
int main(int argc, char **argv)
{
    assert(argc == 4);
    base = argv[1];
    assert(CustomTrack_IsHiddenLev(argv[2]) && !CustomTrack_IsHiddenLev(argv[3]));
    for (unsigned i = 0; i < 2; ++i)
    {
        const std::string key = i ? "visible" : "hidden";
        const auto d = json{{"id", "package/" + key + "/1.0.0"}, {"content_id", key}, {"revision", "1.0.0"},
                            {"display_name", key}, {"author", "Tester"},
                            {"files", json::array({{{"role", "lev"}, {"sha256", argv[i + 2]}, {"bytes", 4u}},
                                                   {{"role", "vrm"}, {"sha256", std::string(64, 'a')}, {"bytes", 4u}}})}};
        const fs::path folder = fs::path(base) / "tracks" / key / "original";
        fs::create_directories(folder);
        std::ofstream(folder / "level.lev") << "lev!";
        std::ofstream(folder / "level.vrm") << "vrm!";
        fs::create_directories(SoloPath("needs-laps"));
        std::ofstream(SoloPath("needs-laps/" + key + "__1.0.0.json")) << d.dump();
    }
    std::vector<APSoloTrackInfo> rows;
    AddNeedsLapsRows(rows);
    assert(rows.size() == 1 && std::strcmp(rows[0].id, "package/visible/1.0.0") == 0);
    assert(fs::is_regular_file(SoloPath("needs-laps/hidden__1.0.0.json")));
    assert(ReadSmallPlainFile(fs::path(base) / "tracks/hidden/original/level.lev", 64) == "lev!");
}
'''
with tempfile.TemporaryDirectory(prefix="ctr-hidden-patch-tracks-") as temp:
    work = Path(temp)
    fake = work / "solo wrapper.py"
    fake.write_text(wrapper)
    subprocess.run([sys.executable, "-I", "-B", "-c", adapter, str(fake), ",".join(hashes),
                    "generate", hashes[-1], visible], check=True)
    subprocess.run([sys.executable, "-I", "-B", "-c", adapter, str(fake), ",".join(hashes),
                    "save-room-yaml", hashes[-1], visible], check=True)
    fake.write_text(wrapper.replace("def _yaml_pick_names", "def unused_alias_resolver")
                    .replace("'old-key', ", ""))
    subprocess.run([sys.executable, "-I", "-B", "-c", adapter, str(fake), ",".join(hashes),
                    "generate", hashes[-1], visible], check=True)
    src, exe = work / "probe.cpp", work / "probe"
    src.write_text(fixture)
    subprocess.run(
        ["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I.", "-Iinclude",
         "-Iap/vendor/json/include", str(src), "-o", str(exe)],
        cwd=root, check=True,
    )
    subprocess.run([str(exe), str(work), hashes[-1], visible], check=True)
