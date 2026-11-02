# Pinned VCDIFF decoder

Unmodified source subset of xdelta 3.1.1 from branch `release3_1_apl`, which the
author relicensed under the Apache License 2.0 (see LICENSE).
Upstream: https://github.com/jmacd/xdelta
Commit: `7508fd2a823443b1f0173ca361620f21d62a7d37`, branch `release3_1_apl`.
Do not replace it with tag `v3.1.0` or the `xdelta-gpl` tree: those are GPLv2.

Used only by the xdelta track importer (`platform/native_xdelta_decode.c`),
in the player client and in the Track Manager builds (`CTR_XDELTA_IMPORT`).
It is built decode only: no encoder, no command-line
main, DJW secondary decompression only (`XD3_ENCODER 0`, `SECONDARY_LZMA 0`,
`SECONDARY_FGK 0`). A patch that needs LZMA or FGK is refused with
"Unsupported patch compression".

`cmake/CustomPackageDeps.cmake` refuses to configure a package build when a
file here differs from `SHA256SUMS`. Updating the decoder needs its own review,
not a silent refresh inside an unrelated change.

SHA-256 pins for the unmodified files:

| File | SHA-256 |
|---|---|
| LICENSE | 6cf63d87586c7c25c3ab8e62eef5c75bbaa982b0a6f9c00c59e2720255aac9ec |
| xdelta3.c | 31a4885ffe63171231482762505210fb29b4ff6c5500676fe0cdf0162ecc715b |
| xdelta3.h | 361d4856fbca7966a2abe2661654df9c9729db32cc376463d2fa7230ad2a5ba9 |
| xdelta3-internal.h | 49559263b0ff78572f873ab4bd21220e04c5a4a5685eda02001cd77d931bf498 |
| xdelta3-list.h | 1f933179e8ebde93c4e55f4b7f6170d2dca7fa535ef188f39a0eccbf7a98b6e5 |
| xdelta3-hash.h | c8fa2a11a6f573c65418cc08dbc7bfe9659d02b30717f24a6bfb691177e0f4c4 |
| xdelta3-cfgs.h | 6ddeb3a90b8d055189fc899ba4d191f0853e95549bc4fefa985c17dd8d5e2124 |
| xdelta3-second.h | f9bef2060b91f99a20bf7d74cd02a020844e10d419cbd04e7be49fb279de6960 |
| xdelta3-djw.h | b6d58e1ed081f9077c875db9382f8ba0c965921c31af914b055d8f68783d9047 |
| xdelta3-decode.h | b7b4f7482808a66be90d5a34edb0d9f4da6b6c22e152b7690b389ecd4ddc838c |
