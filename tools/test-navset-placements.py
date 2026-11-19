"""Verify the grid and crate proposer on flat ground without a game asset."""

import runpy
from pathlib import Path

proposer = runpy.run_path(str(Path(__file__).parent / "navrec" / "propose-placements.py"))
origin = (0, 20, 0)
flat = [
    ((-4096, 20, -4096), (4096, 20, -4096), (-4096, 20, 4096)),
    ((4096, 20, -4096), (-4096, 20, 4096), (4096, 20, 4096)),
]
grid = proposer["propose_grid"](flat, (500, 20, 0), origin)
assert len(grid) == 8
assert [entry["pos"] for entry in grid[:4]] == [(0, 20, z) for z in (192, 0, -192, -384)]
assert [entry["pos"] for entry in grid[4:]] == [(-260, 20, z) for z in (192, 0, -192, -384)]
assert all(entry["rot"] == [0, 0, 0] for entry in grid)

centers = [(0, -1000), (900, -1000), (1500, -500), (1500, 500), (900, 1000),
           (0, 1000), (-900, 1000), (-1500, 500), (-1500, -500), (-900, -1000)]
checkpoints = [(x, 20, z, 5000 - i * 500, (i + 1) % 10, 255, (i - 1) % 10, 255)
               for i, (x, z) in enumerate(centers)]
rows = proposer["propose_crates"](flat, checkpoints)
assert [row["row"] for row in rows] == [1, 2, 3, 4]
assert all(len(row["crates"]) == 4 for row in rows)
assert len({crate["pos"] for row in rows for crate in row["crates"]}) == 16
for row, cp in zip(rows, checkpoints[2::2]):
    positions = [crate["pos"] for crate in row["crates"]]
    assert all(p[1] == 20 for p in positions)
    assert all(abs(sum(p[axis] for p in positions) / 4 - cp[axis]) < 1 for axis in (0, 2))
    assert all(190 <= ((a[0] - b[0]) ** 2 + (a[2] - b[2]) ** 2) ** 0.5 <= 194
               for a, b in zip(positions, positions[1:]))

for transform in (
    lambda: proposer["propose_grid"]([], (500, 20, 0), origin),
    lambda: proposer["propose_crates"]([], checkpoints),
    lambda: proposer["propose_crates"](flat, checkpoints[:5]),
):
    try:
        transform()
    except ValueError:
        pass
    else:
        raise AssertionError("Ungrounded placements and broken checkpoint paths must be refused")

print("Grid and crate rows checked on synthetic ground")
