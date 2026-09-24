# Pinned Unicode implementation

Unmodified utf8proc 2.8.0 source subset, Unicode 15.0.0.
Upstream: https://github.com/JuliaStrings/utf8proc
Commit: `1cb28a66ca79a0845e99433fd1056257456cef8b`, tag `v2.8.0`.
See LICENSE.md for upstream and Unicode data notices.

Used only by the custom-track package layer, which is compiled into the box
authoring build (`CTR_AP_AUTHORING` with `CTR_CUSTOM_TRACKS`). The player client
does not build or link it. Package titles are checked against this explicit
Unicode 15 profile, not the host locale or a system library.

`cmake/CustomPackageDeps.cmake` refuses to configure the authoring build when a
file here differs from `SHA256SUMS`. Updating the profile needs its own review,
not a silent refresh inside an unrelated change.

SHA-256 pins for the unmodified files:

| File | SHA-256 |
|---|---|
| LICENSE.md | 3b510150d34f248a221bb88e1d811238d6c6c18b51231822c42974c39bb07256 |
| utf8proc.c | fdcc214d140a526b0de3ad22d816823c3e7940c017568c046e40f444060e7a51 |
| utf8proc.h | 3ac6bcc03716a8af82ad91d8287186a846209b3aadab04f001004d33bdb7314e |
| utf8proc_data.c | 57a774e8b9b5ae6b2bca3927adee178c20ffcc1986d7e2eff8f39a015f464c88 |
