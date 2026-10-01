// The shared crate spawn transform (declared in ap_box_model.h). Included in
// place by ap_box_model.c, and on its own by the authoring client without
// Archipelago (game/game_unity.h), after ap_box_measure.c.

#if defined(CTR_AP) || defined(CTR_BOX_AUTHORING)

#include <common.h>

#include "ap_box_model.h"
#include "ap_hooks.h" // AP_LogLine

// ── the shared spawn transform ──────────────────────────────────────────────

int AP_BoxModel_BaseOffsetY(struct Model *model)
{
	static int warned;
	int base = 0, top = 0, height = 0;

	if (model == 0)
		return 0;

	if (AP_BoxModel_MeasureVertical(model, &base, &top, &height))
		return base;

	// FAIL CLOSED to the authored anchor: an unmeasurable model spawns exactly
	// where it did before this correction existed, which is a known state rather
	// than a guessed lift. Once per process, because this can only be reached
	// from a model whose command list or frame data this walk does not
	// understand, and that is worth exactly one line, not one per spawn.
	if (!warned)
	{
		warned = 1;
		AP_LogLine("[AP BOX] WARNING: a crate model could not be measured; its spawns fall back to "
		           "the authored anchor with no height correction\n");
	}
	return 0;
}

void AP_BoxModel_SpawnPos(struct Model *model, int x, int y, int z, Vec3 *out)
{
	if (out == 0)
		return;

	out->x = x;
	out->y = y + AP_BoxModel_BaseOffsetY(model);
	out->z = z;
}

#endif
