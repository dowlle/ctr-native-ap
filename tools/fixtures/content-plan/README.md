# Content-plan integration fixtures

`baseline.json` and `starts-extras.json` are actual emitted slot data from the
Generate.main to Main.main room probes on 2026-09-22. Paired apworld base:
`56ad9999b121eddf855377cb580e1e9ded833a28` plus the content-plan implementation.
Both use all 18 retail Trophy tracks and Baby T Park 1.0.2, with identical content
and physical placement. The second changes only exact item extras and starts.

The standalone C++ harness checks structural rejection, registry ownership,
item accounting, room membership and reconnect identity. The parity script
also generates fresh layouts through the paired apworld and compares decisions.
These fixtures do not claim Steam gameplay or live-server acceptance.
