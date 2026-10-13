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

`tools/test-named-blocks.cpp` reads these through the production parser.
These fixtures do not claim Steam gameplay or live-server acceptance.
