// Host harness for the Demo Camera end-of-race table check (A4-33).
//
//   cc -Wall -Wextra -Wno-unused-function -DCTR_AP -DCTR_CUSTOM_TRACKS -I ap -I include -o /tmp/test-democam-eor-table tools/test-democam-eor-table.c
//   /tmp/test-democam-eor-table
//
// Drives AP_DemoCam_EorTableUsable (ap/ap_democam_logic.h) with hand-built
// SpawnType1 tables. The Demo Camera trap starts only when this returns 1;
// otherwise it stays armed. What it pins:
//   * a table with count >= 3 but the ST1_CAMERA_EOR entry NULL is refused
//     (the shape a custom LEV keeps after unmapped entries are cleared);
//   * the entry rule matches CustomTrackPolicy_St1EntryPresent for every count,
//     index and hole pattern, so the two cannot drift;
//   * an EOR table CAM_ThTick cannot use (no cameras, an unknown mode, a
//     checkpoint index past the restart points) is refused;
//   * a retail-shaped table with mixed modes, including negative (flagged)
//     modes, is accepted.

#include <stdio.h>
#include <string.h>

#include "../ap/ap_democam_logic.h"
#include "platform/native_custom_tracks_policy.h"

#define EOR 2

// data.EndOfRace_Camera_Size, copied from game/zGlobal_DATA.c (index 0x11 is
// zero-filled there too).
static const short kSizes[0x12] = {0, -1, -1, 0xC, 0x6, -1, -1, 0x6, 0xC, 0x10, 0x6, 0xC, 0x14, 0x10, 0xC, 0, 0, 0};

static int failures;

static void expect(const char *name, int got, int want)
{
	if (got != want)
	{
		printf("FAIL %-60s got=%d want=%d\n", name, got, want);
		failures++;
	}
	else
		printf("PASS %s\n", name);
}

// A ring of checkpoints laid out like struct CheckpointNode (12 bytes: pos,
// distToFinish, then forward / left / backward / right links at +8).
#define NODE 12
static unsigned char nodes[256 * NODE];

static void ring(int n)
{
	int i;
	memset(nodes, 0, sizeof nodes);
	for (i = 0; i < n; i++)
	{
		nodes[i * NODE + 8] = (unsigned char)((i + 1) % n);  // forward
		nodes[i * NODE + 9] = 0xFF;                          // left
		nodes[i * NODE + 10] = (unsigned char)((i + n - 1) % n); // backward
		nodes[i * NODE + 11] = 0xFF;                         // right
	}
}

static int usable(int count, const void *const *entries, int restarts)
{
	ring(restarts);
	return AP_DemoCam_EorTableUsable(count, entries, EOR, restarts, nodes + 8, NODE, kSizes, 0x12);
}

int main(void)
{
	static short dummy[4];
	// Retail-shaped: three cameras, modes 4 (look-at, 6 bytes), -12 (flagged
	// point path, 20 bytes) and 9 (track path, 16 bytes).
	static short good[] = {
		3,
		5, 4, 100, 200, 300,
		9, -12, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
		20, 9, 0, 1, 2, 3, 4, 5, 6, 7,
	};
	static short zero[] = {0};
	static short badMode[] = {2, 5, 4, 1, 2, 3, 6, 5};          // second camera: mode 5 does not exist
	static short pastTable[] = {1, 5, 0x40};                     // mode index past the size table
	static short badCheckpoint[] = {2, 5, 4, 1, 2, 3, 99, 4, 1, 2, 3}; // second camera: checkpoint 99 of 30
	static short negCheckpoint[] = {1, -1, 0};
	static short mode0[] = {2, 3, 0, 4, 4, 1, 2, 3};             // mode 0 camera beside a shot: retail allows it
	static short onlyChase[] = {2, 3, 0, 7, 0};                  // only mode 0: no visible change
	static short onlyFirstPerson[] = {2, 3, 15, 7, -16};         // modes 15/16: no end-of-race setup
	static short mode17[] = {2, 3, 4, 1, 2, 3, 7, 17};           // zero-filled last size slot, not a mode
	static short huge[] = {1000, 0, 0};
	static short over[] = {65, 0, 4, 1, 2, 3};                   // one past the cap
	static short shadowed[] = {2, 5, 4, 100, 200, 300, 5, 0};    // later chase entry on the same checkpoint wins
	static short notShadowed[] = {2, 5, 0, 5, 4, 100, 200, 300}; // later shot wins
	static short neighbourShadow[] = {2, 5, 4, 1, 2, 3, 6, 0};   // checkpoint 6 links back to 5 and forward to 7: 4 still picks the shot
	static short fullShadow[] = {4, 5, 4, 1, 2, 3, 4, 0, 5, 0, 6, 0}; // chase entries cover 4, 5, 6 after the shot
	const void *entries[8];
	int count, idx, hole;

	// 1. Entry presence.
	memset(entries, 0, sizeof entries);
	entries[0] = dummy;
	entries[1] = dummy;
	entries[3] = dummy;
	expect("count 4, EOR entry NULL is refused", usable(4, entries, 30), 0);
	expect("count 7, EOR entry NULL is refused (Baby T Park shape)", usable(7, entries, 30), 0);
	entries[EOR] = good;
	expect("count 4, EOR entry present and good is accepted", usable(4, entries, 30), 1);
	expect("count 2 is refused even with a pointer in slot 2", usable(2, entries, 30), 0);
	expect("count 3 is enough", usable(3, entries, 30), 1);
	expect("NULL entry array is refused", usable(4, 0, 30), 0);
	expect("no restart points is refused", usable(4, entries, 0), 0);
	expect("no size table is refused", AP_DemoCam_EorTableUsable(4, entries, EOR, 30, nodes + 8, NODE, 0, 0x12), 0);
	expect("no restart point links is refused", AP_DemoCam_EorTableUsable(4, entries, EOR, 30, 0, NODE, kSizes, 0x12), 0);

	// 2. Same rule as CustomTrackPolicy_St1EntryPresent, every count/index/hole.
	{
		int agree = 1;
		for (count = -1; count <= 8; count++)
			for (idx = -1; idx <= 8; idx++)
				for (hole = 0; hole < 2; hole++)
				{
					int policy, ours;
					memset(entries, 0, sizeof entries);
					entries[0] = entries[1] = entries[3] = entries[4] = good;
					entries[2] = hole ? 0 : good;
					entries[5] = entries[6] = entries[7] = good;
					policy = CustomTrackPolicy_St1EntryPresent(count, idx, entries);
					ring(30);
					ours = AP_DemoCam_EorTableUsable(count, entries, idx, 30, nodes + 8, NODE, kSizes, 0x12);
					if (policy != ours)
					{
						printf("  mismatch count=%d index=%d hole=%d policy=%d ours=%d\n", count, idx, hole, policy, ours);
						agree = 0;
					}
				}
		expect("entry rule matches CustomTrackPolicy_St1EntryPresent everywhere", agree, 1);
	}

	// 3. Table contents CAM_ThTick can use.
	memset(entries, 0, sizeof entries);
	entries[0] = entries[1] = entries[3] = dummy;
	entries[EOR] = zero;
	expect("EOR table with zero cameras is refused", usable(4, entries, 30), 0);
	entries[EOR] = badMode;
	expect("unknown mode (size -1) is refused", usable(4, entries, 30), 0);
	entries[EOR] = pastTable;
	expect("mode index past the size table is refused", usable(4, entries, 30), 0);
	entries[EOR] = badCheckpoint;
	expect("checkpoint index past the restart points is refused", usable(4, entries, 30), 0);
	expect("same table is fine when the level has 100 restart points", usable(4, entries, 100), 1);
	entries[EOR] = negCheckpoint;
	expect("negative checkpoint index is refused", usable(4, entries, 30), 0);
	entries[EOR] = mode0;
	expect("mode 0 camera with no data beside a shot is accepted", usable(4, entries, 30), 1);
	entries[EOR] = onlyChase;
	expect("table of only chase-camera (mode 0) entries is refused", usable(4, entries, 30), 0);
	entries[EOR] = onlyFirstPerson;
	expect("table of only modes 15 and 16 is refused", usable(4, entries, 30), 0);
	entries[EOR] = mode17;
	expect("mode 17 is refused", usable(4, entries, 30), 0);
	entries[EOR] = huge;
	expect("camera count far above the cap is refused", usable(4, entries, 30), 0);
	entries[EOR] = over;
	expect("camera count one above the cap (65) is refused", usable(4, entries, 30), 0);
	entries[EOR] = shadowed;
	expect("shot hidden by a later chase entry on the same checkpoint is refused", usable(4, entries, 30), 0);
	entries[EOR] = notShadowed;
	expect("shot listed after a chase entry on the same checkpoint is accepted", usable(4, entries, 30), 1);
	entries[EOR] = neighbourShadow;
	expect("shot still picked on a checkpoint the later chase entry does not reach", usable(4, entries, 30), 1);
	entries[EOR] = fullShadow;
	expect("shot hidden on every checkpoint it covers is refused", usable(4, entries, 30), 0);
	{
		int m, shots = 0;
		for (m = 0; m <= 17; m++)
			shots += AP_DemoCam_EorModeIsShot(m);
		expect("exactly the ten CAM_ThTick switch modes count as shots", shots, 10);
	}
	entries[EOR] = good;
	expect("restart count exactly one past the highest index is accepted", usable(4, entries, 21), 1);
	expect("restart count equal to the highest index is refused", usable(4, entries, 20), 0);

	printf("%s\n", failures ? "FAIL" : "PASS");
	return failures != 0;
}
