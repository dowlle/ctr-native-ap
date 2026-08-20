#!/usr/bin/env python3
"""Bake the AP box face art into the static C atlas the AP build compiles in.

    python convert.py ../../ap/ap_box_texture_data.h

Reads the three PNGs next to this script and writes one 128x64 RGBA atlas as a
C byte array. No PNG decoder is linked into the game: decoding happens here, at
authoring time, using only the Python standard library.

WHY THESE RECTS
---------------
The atlas shape is not free. Before this existed, the AP build harvested the
retail `crate_question` texture out of the player's own game data and packed the
distinct rects its texture layouts referenced, in the order the crate's command
lists reach them. That packing is reproduced here byte for byte so the face
layout the model consumes keeps the exact UV corners, tpage and clut that
shipped, and only the pixels change:

    (  0,  0)  16x16   wood frame        near-LOD header, first rect reached
    ( 16,  0)  64x64   crate face        near-LOD header, the sampled face
    ( 80,  0)  32x32   crate face (far)  far-LOD header

Measured from retail NTSC-U BIGFILE entry 1 (1p level 0), model
`crate_question`: two headers, tpage 0x0069 / 0x0078 / 0x007a, all with
semi-transparency field 3 and 4bpp colour depth. Everything outside those three
rects stays fully transparent, exactly as the harvest left it.

Only the 64x64 rect is sampled by the AP box model today (its layouts all point
at that one face). The other two are packed anyway so the atlas keeps the shape
the renderer and the model layout were verified against, and so a future
per-LOD or wood-framed variant has its pixels already in the right place.
"""

import struct
import sys
import zlib
from pathlib import Path

ATLAS_W = 128
ATLAS_H = 64

HERE = Path(__file__).resolve().parent

# name -> (source png, atlas x, atlas y, rect w, rect h)
RECTS = [
    ("WOOD", "box_pink_highres_outer.png", 0, 0, 16, 16),
    ("FACE", "box_pink_highres_inner.png", 16, 0, 64, 64),
    ("FACE_FAR", "box_pink_lowres.png", 80, 0, 32, 32),
]


# ---------------------------------------------------------------------------
# Minimal PNG reader: 8-bit non-interlaced, all five scanline filters.
# ---------------------------------------------------------------------------
def read_png(path):
    """Return (width, height, rgba_bytes)."""
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit(f"{path.name}: not a PNG")

    pos = 8
    idat = bytearray()
    plte = None
    trns = None
    width = height = depth = color = interlace = None

    while pos + 8 <= len(data):
        length, ctype = struct.unpack_from(">I4s", data, pos)
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if ctype == b"IHDR":
            width, height, depth, color, _comp, _filt, interlace = struct.unpack(">IIBBBBB", body)
        elif ctype == b"PLTE":
            plte = body
        elif ctype == b"tRNS":
            trns = body
        elif ctype == b"IDAT":
            idat += body
        elif ctype == b"IEND":
            break

    if depth != 8:
        raise SystemExit(f"{path.name}: only 8-bit PNGs are supported (got {depth})")
    if interlace:
        raise SystemExit(f"{path.name}: interlaced PNGs are not supported")
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}.get(color)
    if channels is None:
        raise SystemExit(f"{path.name}: unsupported colour type {color}")
    if color == 3 and plte is None:
        raise SystemExit(f"{path.name}: indexed PNG without a palette")

    raw = zlib.decompress(bytes(idat))
    stride = width * channels
    out = bytearray(height * stride)
    prev = bytearray(stride)
    src = 0
    for y in range(height):
        ftype = raw[src]
        src += 1
        line = bytearray(raw[src:src + stride])
        src += stride
        if ftype == 1:
            for i in range(channels, stride):
                line[i] = (line[i] + line[i - channels]) & 0xFF
        elif ftype == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif ftype == 3:
            for i in range(stride):
                left = line[i - channels] if i >= channels else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif ftype == 4:
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                b = prev[i]
                c = prev[i - channels] if i >= channels else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pred) & 0xFF
        elif ftype != 0:
            raise SystemExit(f"{path.name}: bad scanline filter {ftype}")
        out[y * stride:(y + 1) * stride] = line
        prev = line

    rgba = bytearray(width * height * 4)
    for i in range(width * height):
        px = out[i * channels:(i + 1) * channels]
        if color == 6:
            rgba[i * 4:i * 4 + 4] = px
        elif color == 2:
            rgba[i * 4:i * 4 + 3] = px
            rgba[i * 4 + 3] = 0xFF
        elif color == 0:
            rgba[i * 4:i * 4 + 3] = bytes(px) * 3
            rgba[i * 4 + 3] = 0xFF
        elif color == 4:
            rgba[i * 4:i * 4 + 3] = bytes(px[:1]) * 3
            rgba[i * 4 + 3] = px[1]
        else:  # indexed
            idx = px[0]
            rgba[i * 4:i * 4 + 3] = plte[idx * 3:idx * 3 + 3]
            rgba[i * 4 + 3] = trns[idx] if trns is not None and idx < len(trns) else 0xFF
    return width, height, bytes(rgba)


def resample_nearest(pixels, sw, sh, dw, dh):
    """Nearest-neighbour, so hard pixel edges stay hard. Only used when a source
    PNG does not already match its rect."""
    out = bytearray(dw * dh * 4)
    for y in range(dh):
        sy = min(sh - 1, (y * sh) // dh)
        for x in range(dw):
            sx = min(sw - 1, (x * sw) // dw)
            s = (sy * sw + sx) * 4
            d = (y * dw + x) * 4
            out[d:d + 4] = pixels[s:s + 4]
    return bytes(out)


def main(argv):
    if len(argv) != 2:
        raise SystemExit(f"usage: {argv[0]} <output header path>")
    out_path = Path(argv[1])

    atlas = bytearray(ATLAS_W * ATLAS_H * 4)
    notes = []
    for name, png, ax, ay, w, h in RECTS:
        src = HERE / png
        sw, sh, pixels = read_png(src)
        if (sw, sh) != (w, h):
            notes.append(f"{png}: {sw}x{sh} resampled (nearest) to {w}x{h}")
            pixels = resample_nearest(pixels, sw, sh, w, h)
        else:
            notes.append(f"{png}: {sw}x{sh}, exact fit")
        if ax + w > ATLAS_W or ay + h > ATLAS_H:
            raise SystemExit(f"{name} rect does not fit the atlas")
        for y in range(h):
            d = ((ay + y) * ATLAS_W + ax) * 4
            s = y * w * 4
            atlas[d:d + w * 4] = pixels[s:s + w * 4]

    lines = []
    lines.append("// GENERATED by tools/apbox-texture/convert.py -- do not edit by hand.")
    lines.append("//")
    lines.append("// The AP box face atlas: a 128x64 RGBA image compiled into the AP build, so")
    lines.append("// the box has its own art instead of pixels harvested out of the player's")
    lines.append("// retail game data. See tools/apbox-texture/README.md for the rect map, the")
    lines.append("// source art and the license this art is used under.")
    lines.append("//")
    for note in notes:
        lines.append(f"// {note}")
    lines.append("")
    lines.append("#ifndef AP_BOX_TEXTURE_DATA_H")
    lines.append("#define AP_BOX_TEXTURE_DATA_H")
    lines.append("")
    lines.append(f"#define AP_BOX_TEXTURE_ATLAS_W {ATLAS_W}")
    lines.append(f"#define AP_BOX_TEXTURE_ATLAS_H {ATLAS_H}")
    lines.append("")
    for name, _png, ax, ay, w, h in RECTS:
        lines.append(f"#define AP_BOX_TEXTURE_{name}_X {ax}")
        lines.append(f"#define AP_BOX_TEXTURE_{name}_Y {ay}")
        lines.append(f"#define AP_BOX_TEXTURE_{name}_W {w}")
        lines.append(f"#define AP_BOX_TEXTURE_{name}_H {h}")
        lines.append("")
    lines.append("// RGBA8, row-major from the top-left, alpha preserved from the source art.")
    lines.append("static const unsigned char s_apBoxTextureAtlas"
                 "[AP_BOX_TEXTURE_ATLAS_W * AP_BOX_TEXTURE_ATLAS_H * 4] = {")
    for i in range(0, len(atlas), 16):
        chunk = ",".join(f"0x{b:02x}" for b in atlas[i:i + 16])
        lines.append(f"\t{chunk},")
    lines.append("};")
    lines.append("")
    lines.append("#endif // AP_BOX_TEXTURE_DATA_H")

    out_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    opaque = sum(1 for i in range(3, len(atlas), 4) if atlas[i] != 0)
    print(f"wrote {out_path} ({len(atlas)} atlas bytes, {opaque} non-transparent texels)")
    for note in notes:
        print(f"  {note}")


if __name__ == "__main__":
    main(sys.argv)
