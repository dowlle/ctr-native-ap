# AP box face texture

`convert.py` bakes the AP box art into `ap/ap_box_texture_data.h`, the static
128x64 RGBA atlas the AP build compiles in and uploads as its sideload texture.
The engine never decodes a PNG: decoding happens here, at authoring time, with
the Python standard library only.

## Regenerating

```
cd tools/apbox-texture
python convert.py ../../ap/ap_box_texture_data.h
```

## Validating

```
python validate.py ../../ap/ap_box_texture_data.h
```

`validate.py` checks the generated data against what `ap/ap_box_texture.c` and
the renderer require: the declared atlas size, an array of exactly the right
length, every rect inside the atlas and non-overlapping, no rect left blank, and
the pixels of each rect matching the source PNG they came from. It cannot tell
you the box looks right in a race. It tells you the data is well-formed and
faithful to the art, which is the failure mode that otherwise shows up only as a
box drawn in the wrong colours or not drawn at all.

## The rect map

| Rect in the atlas | Source file | Used by |
|---|---|---|
| 16x16 at (0,0) | `box_pink_highres_outer.png` | the border ring of the framed box; replaced at runtime by the recoloured disc wood (below), kept as the fallback |
| 64x64 at (16,0) | `box_pink_highres_inner.png` | the face square of the framed box |
| 32x32 at (80,0) | `box_pink_lowres.png` | nothing yet (far-LOD face) |

All three source PNGs already match their rect exactly, so nothing is resampled.
`convert.py` resamples (nearest-neighbour, to keep hard pixel edges hard) if a
replacement file ever does not match, and says so in its output and in the
generated header's comment block.

That packing is not arbitrary. Before this existed, the AP build harvested the
retail `crate_question` texture out of the player's own game data and packed the
distinct rects its layouts referenced, in the order its command lists reach
them. Reproducing the same three rects at the same three origins keeps the
model's texture layout byte-identical to what shipped, so replacing the art
cannot also mirror, rotate or resize the face. Verified against retail NTSC-U
BIGFILE entry 1 (1p level 0): `crate_question`, two headers, a 64x64 face and a
16x16 wood rect in the near-LOD header and a 32x32 face in the far-LOD header.

Only the 64x64 face is sampled today: the box model gives all twelve of its
triangles the same layout, whose corners are the face rect's top-left,
bottom-left and top-right. The other two rects are packed anyway so the atlas
keeps the shape the renderer was verified against, and so a later per-LOD or
wood-framed variant has its pixels already in place.

## The framed box and its wood border

The box is built like the retail "?" crate: `gen_framed_model.py` generates
`ap/ap_box_model_framed_data.h`, where every side is the face in a centre square
and a border ring of four strips sampling the 16x16 rect at (0,0). No retail
data is copied into the model.

The border's pixels do not ship either. At runtime `ap/ap_box_texture.c` reads
the retail crate's 16x16 wood tile from the player's own disc (BIGFILE, track 0
1p level and texture file, located through that level's `crate_question`),
recolours it to the box's pink with `AP_BoxEdge_Recolour`
(`ap/ap_box_edge_logic.h`, pinned by `tools/test-box-edge.c`), and writes it
into the atlas before the one-time upload. This happens once, on the first idle
frame, so relic races, which carry no crate, get it too. If the read fails, the
compiled `box_pink_highres_outer.png` border stays.

## The crate face behind the logo

The same one-time disc read takes the retail Wumpa crate's 64x64 face (slats,
no picture), recolours it with the same function as the wood, and puts the six
Archipelago circles on top. `gen_logo_mask.py` generates
`ap/ap_box_logo_mask_data.h` from `box_pink_highres_inner.png`: which face pixels
are the circles and their outline (a flood fill from the border that stops at
the outline). Only those pixels keep JurnthReinal's art. If the face cannot be
read, the face stays JurnthReinal's. No retail pixels are compiled in.

## Colours

The atlas uploaded at runtime is 256x128, not the compiled 128x64: it holds the
box five times, in 80x64 slots (wood at the slot's (0,0), face at (16,0)),
three per row. Slot 0 is the default pink, at the compiled atlas's own
positions; the others are the Archipelago item colours, progression `AF99EF`,
useful `6D8BE8`, filler `00EEEE` and trap `FA8072`, recoloured with
`AP_BoxEdge_RecolourTo`. The seed option `color_boxes_by_item` decides whether
a box wears its item's colour; logic and the atlas layout are in
`ap/ap_box_colour_logic.h`, pinned by `tools/test-box-colour.c`. Without the
disc read, the coloured slots recolour JurnthReinal's border and face instead.

## A note on the source art's edge pixels

Every file in the contributed set carries a magenta (`#FF00FF`) one-pixel band
down its right-hand column and along its bottom row. It is present in all four
default colours and all three item-class colours, so it is systematic, not
design. The retail face it replaces has ordinary art in those pixels, and the
design target `question-crate-face-original-64x64.png` is a pixel-exact render
of that retail face with no such band, so the band is not a transparency key
either.

The art is baked exactly as delivered rather than repaired here, because
altering a contributor's asset on a guess is worse than carrying a known mark.
The current UV mapping samples only two of those texels (the triangle's
top-right and bottom-left corner vertices), so the visible cost today is
negligible. Confirm the band with the artist before any future change widens the
sampled area -- a per-class or full-quad mapping would put the whole band on
screen.

## Attribution

Both credits are permanent and belong in `THIRD_PARTY_NOTICES.md`, the in-game
credits and the release notes:

- Box art: JurnthReinal.
- The Archipelago icon it derives from: (c) 2022 Krista Corkos and Christopher
  Wilson, CC BY-NC 4.0 -- attribution required, non-commercial only. The license
  text as supplied with the art is `LICENSE-archipelago-icon.txt` in this
  directory.

Credit the artist as "JurnthReinal" and by no other name.
