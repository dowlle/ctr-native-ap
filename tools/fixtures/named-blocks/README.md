# Named-block and room-title fixtures (#306)

Slot data emitted by the paired apworld branch `feat/306-named-blocks`
(off `dev/custom-pad-0.3.0`) on 2026-10-13, with the generator script kept
outside the repository:

- `content-plan-generic.json`: the content-plan profile with Baby T Park 1.0.2
  on its generic slot code `35016300` and `room_title` "Baby T Park" (path B+).
- `content-plan-named.json`: the same profile generated with the apworld's
  TEST-ONLY fixture registry, which gives track key `baby-t-park` named block 0
  under the name "Fixture Speedway". The Trophy Race is `35200000`. This is
  test data, not a shipped name.
- `legacy-titles.json`: a schema-18 seed with the legacy `custom_tracks`
  option, a descriptor title "Crash Cove" and the `custom_track_titles` block
  the apworld emits for it ("Crash Cove (Custom)").

- `legacy-named.json` (#133, apworld branch `feat/133-legacy-named-blocks`,
  2026-10-16): a schema-18 seed with the legacy `custom_tracks` option for
  Baby T Park 1.0.2, which has named block 8 in the shipped registry. The entry
  carries `named_block: 8`; Trophy, podium, CTR Token Challenge, letters and
  Reach 10 Wumpa all use the block's codes (35201600 onwards, letter items
  35100040 onwards). Options: lettersanity locations and items, two letters,
  per-track Wumpa, podium checks.

Since the #133 PR 2 review native holds the apworld's named-block table, so
both content-plan fixtures were edited by hand: `content-plan-named.json` now
puts Baby T Park on its shipped block 8 ("Baby T Park", Trophy `35201600`)
instead of the test-only block 0, and `content-plan-generic.json` uses the
unregistered key `fixture-park` and the title "Fixture Park" (a generic track
may not take a named track's name), still on generic slot code `35016300`.

`legacy-titles.json` predates #133: it is the generic slot path (B+) and still
admits, since an entry without `named_block` keeps generic codes.

`tools/test-named-blocks.cpp` reads these through the production parser.
These fixtures do not claim Steam gameplay or live-server acceptance.
