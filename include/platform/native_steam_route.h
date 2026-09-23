#ifndef NATIVE_STEAM_ROUTE_H
#define NATIVE_STEAM_ROUTE_H

// Steam route for room links (issue #334, slice 4).
//
// Steam gives a game it starts the SteamGameId environment variable; for a
// non-Steam shortcut that is the 64-bit id steam://rungameid/<id> starts the
// shortcut with (measured on Windows, 2026-09-18). A client started from Steam
// records the id in the per-install state directory; a later room link that
// finds no running client publishes its request, asks Steam to start that
// shortcut, and exits, so the game runs inside Steam with its overlay and
// input configuration.
//
// The raw environment value is never logged. Only a value that parses as a
// nonzero unsigned 64-bit decimal is used or stored.

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NATIVE_STEAM_ROUTE_FILE "steam-route.txt"
#define NATIVE_STEAM_ROUTE_URL_MAX 48

// 1 to 20 ASCII decimal digits, nonzero, at most 18446744073709551615. No sign,
// whitespace or other bytes. Returns 1 and *out on success; *out is untouched
// otherwise.
int NativeSteamRoute_ParseGameId(const char *text, unsigned long long *out);

// "steam://rungameid/<id>". Returns 1 when it fits and id is nonzero.
int NativeSteamRoute_FormatUrl(unsigned long long id, char *out, size_t cap);

// The recorded id in stateDir/steam-route.txt ("<id>\n"). Load returns 1 and
// *out only for a valid id; a missing or damaged file is "no route". Save
// writes an exclusive temp file, flushes it and renames it into place.
int NativeSteamRoute_Load(const char *stateDir, unsigned long long *out);
int NativeSteamRoute_Save(const char *stateDir, unsigned long long id);

#ifdef __cplusplus
}
#endif

#endif // NATIVE_STEAM_ROUTE_H
