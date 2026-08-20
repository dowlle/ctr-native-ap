#!/usr/bin/env python3
"""Render the generated atlas back out to a PNG, so the bytes that ship can be
looked at without launching the game.

    python preview.py ../../ap/ap_box_texture_data.h atlas-preview.png [scale]

The image is built from the C array itself, not from the source PNGs, so it
shows what the binary actually carries. Scale is nearest-neighbour and defaults
to 1; pass 8 for something a person can see. Standard library only.
"""

import re
import struct
import sys
import zlib
from pathlib import Path


def main(argv):
    if not 3 <= len(argv) <= 4:
        raise SystemExit(f"usage: {argv[0]} <generated header> <output png> [scale]")
    header, out_path = Path(argv[1]), Path(argv[2])
    scale = int(argv[3]) if len(argv) == 4 else 1

    src = header.read_text(encoding="utf-8")
    w = int(re.search(r"#define AP_BOX_TEXTURE_ATLAS_W (\d+)", src).group(1))
    h = int(re.search(r"#define AP_BOX_TEXTURE_ATLAS_H (\d+)", src).group(1))
    body = src[src.index("s_apBoxTextureAtlas"):]
    body = body[body.index("{") + 1:body.index("};")]
    atlas = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]{2})", body))
    if len(atlas) != w * h * 4:
        raise SystemExit(f"array is {len(atlas)} bytes, expected {w * h * 4}")

    ow, oh = w * scale, h * scale
    raw = bytearray()
    for y in range(oh):
        raw.append(0)  # filter type 0
        row = atlas[(y // scale) * w * 4:((y // scale) + 1) * w * 4]
        if scale == 1:
            raw += row
        else:
            for x in range(ow):
                s = (x // scale) * 4
                raw += row[s:s + 4]

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", ow, oh, 8, 6, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
           + chunk(b"IEND", b""))
    out_path.write_bytes(png)
    print(f"wrote {out_path} ({ow}x{oh}, scale {scale})")


if __name__ == "__main__":
    main(sys.argv)
