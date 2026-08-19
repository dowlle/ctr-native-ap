# AP-logo marker model

`convert.py` turns MMRecompRando's N64 Archipelago-logo mesh into the static C
data that ctr-native compiles into `ap/ap_marker_model_data.h`.

## Regenerating

```
cd tools/aplogo
python convert.py ../../ap/ap_marker_model_data.h
```

`aplogo.h` is the vendored source mesh, taken verbatim from
`RecompRando/MMRecompRando` at `include/aplogo.h`.

## Validating

```
python validate.py ../../ap/ap_marker_model_data.h
```

`validate.py` replays `RenderBucket_DrawFunc_Normal`'s command-stream semantics
over the generated data and checks the invariants the engine depends on: the
colour count fits the scratchpad cache, no command is misread as a colour-only
command, every texture index is 0 so the untextured `POLY_G3` writer is
selected, vertex consumption matches the supplied vertex count exactly, and the
strip machine emits the expected triangle count.

It cannot tell you the marker looks right. It tells you the data is well-formed
against the decoder, which is the failure mode that otherwise shows up only as a
marker that silently does not draw.

## Why this mesh works untextured

Their display list draws the logo with `gsDPSetCombineLERP(... SHADE ...
COMBINED, 0, PRIMITIVE, 0 ...)` and never samples a texel, so the six logo
regions are flat primitive colours over a shaded silhouette. The generated model
keeps both flat faces but omits the source mesh's thin extrusion rim: a pad may
render three markers together, and the full 216-triangle mesh consumed 18,144
bytes of the hub's primitive memory in that case. The silhouette lives in the
geometry rather than in a texture, which is what lets it survive the trip into
ctr-native's untextured `POLY_G3` path.

The six region colours are stored here as **luminance**, not as the logo's real
colours. The ruled design requires a near-neutral base so the per-class tint
modulates cleanly; a coloured base would muddy every class tint.

## Attribution

Both credits are permanent and belong in `THIRD_PARTY_NOTICES` and the release
notes:

- Mesh: MMRecompRando (`github.com/RecompRando/MMRecompRando`), GPL-3.0.
- The Archipelago logo itself: (c) 2022 Krista Corkos and Christopher Wilson,
  CC BY-NC 4.0 — attribution required, non-commercial only.

Both credits are recorded in `THIRD_PARTY_NOTICES.md`. Keep that notice with
every source and binary distribution that contains the marker.
