#include <common.h>

#ifdef CTR_AP
// Keep AP bookkeeping outside the retail overlay and weapon object layouts.
struct BossMineProgress
{
	s64 distance;
	Vec3 position;
	SVec3 backward;
	int remaining;
	int checkpoint;
	int pendingCheckpoint;
	int valid;
	int readyForDrop;
};

struct BossMine
{
	struct Instance *inst;
	struct Driver *owner;
	s64 dropDistance;
	Vec3 dropPosition;
	SVec3 backward;
};

static struct BossMineState
{
	struct BossMineProgress progress[8];
	struct BossMine mines[sizeof D231.minePoolItem / sizeof D231.minePoolItem[0]];
} s_bossMineState;
static struct Driver *s_bossMineShooter;

static void RB_MinePool_ResetBossProgress(void)
{
	memset(&s_bossMineState, 0, sizeof s_bossMineState);
	s_bossMineShooter = NULL;
}

void RB_MinePool_BossShot(struct Driver *driver)
{
	s_bossMineShooter = driver && AP_BossMineExpiryNow() ? driver : NULL;
}

void RB_MinePool_BossMine(struct MineWeapon *mw, struct Instance *inst)
{
	struct Driver *owner = s_bossMineShooter;
	if (owner == NULL || mw->instParent != owner->instSelf || owner->driverID >= 8)
		return;
	struct BossMineProgress *progress = &s_bossMineState.progress[owner->driverID];
	if (!progress->readyForDrop)
		return;

	struct BossMine *mine = &s_bossMineState.mines[mw->weaponSlot231 - D231.minePoolItem];
	*mine = (struct BossMine){inst, owner, progress->distance, progress->position, progress->backward};
}

static int RB_MinePool_LeadsTo(const struct CheckpointNode *nodes, int count, int from, int to, int hops)
{
	if (from < 0 || from >= count)
		return 0;
	if (from == to)
		return 1;
	if (hops == 0)
		return 0;
	return RB_MinePool_LeadsTo(nodes, count, nodes[from].nextIndex_forward, to, hops - 1) ||
	       RB_MinePool_LeadsTo(nodes, count, nodes[from].nextIndex_left, to, hops - 1) ||
	       RB_MinePool_LeadsTo(nodes, count, nodes[from].nextIndex_right, to, hops - 1);
}

void RB_MinePool_BossProgress(struct Driver *driver, int remaining, int length, int checkpoint, const SVec3 *backward)
{
	if ((sdata->gGT->gameMode1 & (ADVENTURE_BOSS | END_OF_RACE | PAUSE_ALL)) != ADVENTURE_BOSS ||
	    !(driver->actionsFlagSet & ACTION_BOT) || driver->driverID >= 8)
		return;
	struct BossMineProgress *progress = &s_bossMineState.progress[driver->driverID];
	if (!backward || length <= 0 || driver->kartState == KS_MASK_GRABBED || !(backward->x || backward->y || backward->z))
	{
		progress->valid = 0;
		progress->readyForDrop = 0;
		return;
	}

	// Unwrap each adjacent sample in either direction. Lap scoring and race rank
	// are deliberately independent: short custom laps can wrap without retail's
	// finish thresholds, and reversing across the line must subtract progress.
	if (remaining < 0)
		remaining += length;
	Vec3 position = { .x = CTR_MipsSra(driver->posCurr.x, 8), .y = CTR_MipsSra(driver->posCurr.y, 8),
	                  .z = CTR_MipsSra(driver->posCurr.z, 8) };
	Vec3 motion = { .x = position.x - progress->position.x, .y = position.y - progress->position.y,
	                .z = position.z - progress->position.z };
	int movement = abs(motion.x) + abs(motion.y) + abs(motion.z);
	int advance = progress->remaining - remaining;
	if (advance > length / 2)
		advance -= length;
	else if (advance < -length / 2)
		advance += length;

	int relocation = !progress->valid || movement > 1024;
	int continuous = !relocation && movement != 0 && abs(advance) * 2 < length;
	int opposite = 0;
	if (!relocation && movement != 0 && abs(advance) > movement + 64)
	{
		// Authored distances can jump when a short piece of road hands off to
		// its next checkpoint. Allow the jump when the graph and the kart's
		// movement agree. Keep the search bounded for forks and dense layouts.
		const struct Level *level = sdata->gGT->level1;
		int forward = -(motion.x * (progress->backward.x + backward->x) +
		                motion.y * (progress->backward.y + backward->y) +
		                motion.z * (progress->backward.z + backward->z));
		int from = advance > 0 ? progress->checkpoint : checkpoint;
		int to = advance > 0 ? checkpoint : progress->checkpoint;
		opposite = (advance > 0 && forward < 0) || (advance < 0 && forward > 0);
		continuous = continuous && from != to && abs(advance) < length / 4 &&
		             ((advance > 0 && forward > 0) || (advance < 0 && forward < 0)) &&
		             level && level->ptr_restart_points &&
		             RB_MinePool_LeadsTo(level->ptr_restart_points, level->cnt_restart_points, from, to, 4);
	}
	progress->position = position;
	if (!relocation && !continuous && advance != 0)
	{
		// Retain the accepted reference when the label flickers or disagrees
		// with motion. An unconnected label must persist over moving samples
		// before it can become the new reference, without crediting its jump.
		if (opposite || movement == 0 || checkpoint != progress->pendingCheckpoint)
		{
			progress->pendingCheckpoint = opposite || movement == 0 ? -1 : checkpoint;
			progress->readyForDrop = 0;
			return;
		}
	}
	if (continuous)
		progress->distance += advance;
	progress->remaining = remaining;
	progress->checkpoint = checkpoint;
	progress->pendingCheckpoint = -1;
	progress->backward = *backward;
	progress->valid = 1;
	progress->readyForDrop = 1;
	if (!continuous || advance <= 0)
		return;

	for (struct WeaponSlot231 *slot = (struct WeaponSlot231 *)D231.minePoolTaken.first; slot != NULL;)
	{
		struct WeaponSlot231 *next = slot->next;
		const struct BossMine *mine = &s_bossMineState.mines[slot - D231.minePoolItem];
		if (mine->owner == driver)
		{
			const s32 *pos = mine->inst->matrix.t;
			// Measure the item's position along the drop segment. A tossed
			// item can move before settling, so use its current position.
			s64 offset = -(((s64)pos[0] - mine->dropPosition.x) * mine->backward.x +
			               ((s64)pos[1] - mine->dropPosition.y) * mine->backward.y +
			               ((s64)pos[2] - mine->dropPosition.z) * mine->backward.z) / 4096;
			if (offset < -length / 8)
				offset = -length / 8;
			if (offset > length / 8)
				offset = length / 8;
			if (2 * (progress->distance - mine->dropDistance - offset) >= length)
			{
				struct MineWeapon *weapon = slot->mineWeapon;
				RB_MinePool_Remove(weapon);
				weapon->boolDestroyed = MINE_DESTROYED_EXPIRED;
			}
		}
		slot = next;
	}
}
#endif

// NOTE(aalhendi): ASM-verified against NTSC-U 926 overlay 231 0x800abfec-0x800ac0e4.
void RB_MinePool_Init(void)
{
	int i;
	int numMines;
	int gameMode;

	LIST_Clear(&D231.minePoolTaken);
	LIST_Clear(&D231.minePoolFree);
#ifdef CTR_AP
	RB_MinePool_ResetBossProgress();
#endif

	gameMode = sdata->gGT->gameMode1;

	// default
	numMines = 10;

	if ((gameMode & CRYSTAL_CHALLENGE) != 0)
	{
		// naughty dog bug, should be 50,
		// this caused nitro court challenge bug
		numMines = 40;
	}

	// boss race
	if ((gameMode & ADVENTURE_BOSS) != 0)
	{
		int bossLevel = sdata->gGT->levelID;
#ifdef CTR_AP
		// #169: the pool size belongs to the boss's inherited attack, so a
		// shuffled venue keeps its garage's home-track pool.
		bossLevel = AP_BossRetailRulesLevel(bossLevel);
#endif
		if (bossLevel == DRAGON_MINES)
		{
			numMines = 3;
		}
		if (bossLevel == ROO_TUBES)
		{
			numMines = 7;
		}
	}

	// add all mines
	for (i = 0; i < numMines; i++)
	{
		LIST_AddFront(&D231.minePoolFree, &D231.minePoolItem[i].item);
	}
}

// NOTE(aalhendi): ASM-verified against NTSC-U 926 overlay 231 0x800ac0e4-0x800ac13c.
void RB_MinePool_Remove(struct MineWeapon *mw)
{
	struct WeaponSlot231 *ws = mw->weaponSlot231;

	if (ws == NULL)
	{
		return;
	}

#ifdef CTR_AP
	s_bossMineState.mines[ws - D231.minePoolItem] = (struct BossMine){0};
	struct Instance *crateInst = mw->crateInst;
	if (crateInst && crateInst->model &&
	    (crateInst->model->id == PU_FRUIT_CRATE || crateInst->model->id == PU_RANDOM_CRATE) &&
	    crateInst->thread && crateInst->thread->object)
	{
		struct Crate *crate = crateInst->thread->object;
		crate->boolPauseCooldown = 0;
	}
	mw->crateInst = NULL;
#endif
	// remove from taken, add to free
	LIST_RemoveMember(&D231.minePoolTaken, (struct Item *)ws);
	LIST_AddFront(&D231.minePoolFree, (struct Item *)ws);

	mw->boolDestroyed = 1;
	mw->weaponSlot231 = NULL;
}

// NOTE(aalhendi): ASM-verified against NTSC-U 926 overlay 231 0x800ac13c-0x800ac1b0.
void RB_MinePool_Add(struct MineWeapon *mw)
{
	struct WeaponSlot231 *ws;

	// if no more items on free list
	if ((&D231.minePoolFree)->count == 0)
	{
		// remove oldest mine
		RB_MinePool_Remove(((struct WeaponSlot231 *)(&D231.minePoolTaken)->last)->mineWeapon);
	}

	// LIST_RemoveBack free list
	ws = (struct WeaponSlot231 *)LIST_RemoveBack(&D231.minePoolFree);

	// link together
	ws->mineWeapon = mw;
	mw->weaponSlot231 = ws;

	// LIST_AddFront to taken list
	LIST_AddFront(&D231.minePoolTaken, (struct Item *)ws);
}
