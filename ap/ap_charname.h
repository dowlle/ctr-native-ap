#ifndef AP_CHARNAME_H
#define AP_CHARNAME_H

// ---------------------------------------------------------------------------
// The character picker's portrait fallback, as a pure function (#54/#209).
//
// A picker tile draws gGT->ptrIcons[MetaDataCharacters[id].iconID]. That table
// is rebuilt per level from the level's own icon table plus the resident MPK
// (DecalGlobal_Store, game/DecalGlobal.c:19-49), so whether every character
// portrait is resident in a given adventure hub is a runtime data fact, not a
// source fact. The picker therefore has to degrade to a name.
//
// It lives in its own header so tools/test-character-fallback.cpp exercises the
// SAME code the picker calls, rather than a restatement of it that can drift.
// It is header-only and dependency-free for exactly that reason: the harness
// must not have to link the engine to test a string choice.
//
// The rule: prefer the localised name, accept the EXE's debug name when the
// localised one is absent or empty, and never return NULL. DecalFont_DrawLine
// walks the string it is handed (game/DecalFont.c:125), so NULL is not a
// survivable argument -- "draws nothing" would have been the good outcome.
// ---------------------------------------------------------------------------

// Last-resort label. Deliberately not empty: a tile that draws nothing is
// indistinguishable from a tile that is not there, and the residency question
// this fallback exists to answer needs a visible answer.
#define AP_CHARNAME_UNKNOWN "?"

static inline const char *AP_CharName_Pick(const char *localised, const char *debugName)
{
	if (localised != 0 && localised[0] != '\0')
		return localised;
	if (debugName != 0 && debugName[0] != '\0')
		return debugName;
	return AP_CHARNAME_UNKNOWN;
}

#endif // AP_CHARNAME_H
