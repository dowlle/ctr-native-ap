# Authoring client branch

This branch is CTR Native with this fork's custom track system and authoring
tools, without the Archipelago client. It was generated from `6bdae6e3c`
(`6bdae6e3c7129770a0201919efa965af569c141b`) by `tools/authoring-branch/make-branch.py`: that release
plus one commit that removes the Archipelago-only sources under `ap/` and makes
the authoring client the default build. Do not commit to it by hand; it is
regenerated from each release tag of `dowlle/ctr-native-ap`.

What it builds (`ctr_native_authoring`):

- the custom track loader with the 8 MiB memory arena it needs,
- the package library, Track Manager (`Options > Custom Content`), Arcade and
  Time Trial custom pages, Project Saphi catalogue and install, custom music,
- Box Author Mode (`Options > Authoring`), which writes box placement files,
- the AI lap recorder.

How the custom track system works, file by file:
[`docs/CUSTOM_TRACKS.md`](docs/CUSTOM_TRACKS.md).

## Build (Linux)

```
bash ap/vendor/fetch-deps.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS='-m32 -msse' -DCMAKE_CXX_FLAGS=-m32
cmake --build build --parallel 2
```

You need 32-bit OpenSSL and zlib development packages. Windows uses MinGW32,
see `tools/ci/build-clients.sh windows`. `-DCTR_AUTHORING_CLIENT=OFF` builds
plain CTR Native. Put a raw `.bin` image of your own NTSC-U disc in
`build/assets/` before starting the game.

`#ifdef CTR_AP` blocks remain in shared engine files. They compile to nothing
here, and keeping them makes this branch a mechanical copy of the release.

## License

GPL-3.0, the same as upstream CTR Native. The Archipelago logo mesh used as
Box Author Mode's fallback marker is CC BY-NC 4.0; its attributions are in
`THIRD_PARTY_NOTICES.md` and must stay with it.
