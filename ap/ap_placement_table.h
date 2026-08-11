#ifndef AP_PLACEMENT_TABLE_H
#define AP_PLACEMENT_TABLE_H

// Which box-placement table is live, and how to read it (#109 packaging).
//
// The client carries TWO placement tables and exactly one of them is live at a
// time: the FINAL authored set compiled into the executable
// (ap_placements_data.h), and an external AP_AUTHOR_FILE next to the executable.
// This header owns the rule that decides between them and the read-only view
// over the winner.
//
// Deliberately freestanding, for the same reason ap_box_map.h is: no engine
// types, no IO, no globals. The owning state lives in ap_author.c (which table
// was found, and the file's rows); everything here is pure over what it is
// handed, so tools/test-box-map.c exercises the REAL precedence rule out of
// engine rather than a second copy of it that can drift.
//
// Compiled ONLY when CTR_AP is defined, like the rest of ap/.

#ifdef CTR_AP

#include "ap_placements_data.h" // the compiled-in default set

// ── the rule ────────────────────────────────────────────────────────────────
//
// PRECEDENCE IS BY EXISTENCE, NOT BY CONTENT. If the external file opened, the
// file is the table, even when it parses to zero rows. An operator who empties
// the file means zero boxes, and silently resurrecting the compiled-in set
// behind their back would be the worse failure; the zero case is logged loudly
// instead (AP_AuthorLoad). The override is WHOLESALE, never a merge or a patch:
// slot assignment is positional (ap_box_map.h), so a table that is part file and
// part default would re-point names against both.
#define AP_PLACEMENT_SRC_EMBEDDED 0
#define AP_PLACEMENT_SRC_FILE     1

// One placement, in exactly the shape the LEV stores one: three signed 16-bit
// world coordinates plus an engine angle. Both tables use it, which is what lets
// the view below hand out rows from either without a conversion step.
typedef struct
{
	short level;
	short x, y, z, rotY;
} AP_PlacementRow;

// The live table: a source tag plus the file's rows. The embedded rows are not
// carried here because they are a compile-time constant this header already
// sees; `file` and `fileCount` are meaningful only when source is _SRC_FILE.
typedef struct
{
	int                    source;
	const AP_PlacementRow *file;
	int                    fileCount;
} AP_PlacementTable;

// How many rows the live table holds.
static inline int AP_PlacementTable_Count(const AP_PlacementTable *t)
{
	if (t == 0)
		return 0;
	if (t->source == AP_PLACEMENT_SRC_FILE)
		return (t->file == 0) ? 0 : t->fileCount;
	return AP_EMBEDDED_PLACEMENT_COUNT;
}

// Copy row `index` out of the live table. Any out pointer may be NULL. Returns 0
// when the index is out of range, in which case nothing is written.
static inline int AP_PlacementTable_Get(const AP_PlacementTable *t, int index,
                                        int *level, short *x, short *y, short *z, short *rotY)
{
	short lvl, rx, ry, rz, rrot;

	if (index < 0 || index >= AP_PlacementTable_Count(t))
		return 0;

	if (t->source == AP_PLACEMENT_SRC_FILE)
	{
		const AP_PlacementRow *r = &t->file[index];
		lvl = r->level;
		rx = r->x;
		ry = r->y;
		rz = r->z;
		rrot = r->rotY;
	}
	else
	{
		const AP_EmbeddedPlacement *e = &AP_EMBEDDED_PLACEMENTS[index];
		lvl = e->level;
		rx = e->x;
		ry = e->y;
		rz = e->z;
		rrot = e->rotY;
	}

	if (level != 0)
		*level = (int)lvl;
	if (x != 0)
		*x = rx;
	if (y != 0)
		*y = ry;
	if (z != 0)
		*z = rz;
	if (rotY != 0)
		*rotY = rrot;

	return 1;
}

#endif // CTR_AP
#endif // AP_PLACEMENT_TABLE_H
