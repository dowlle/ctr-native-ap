#ifndef AP_VERSION_CMP_H
#define AP_VERSION_CMP_H

// Pair-version comparator for the update notice (issue #150).
//
// Deliberately header-only freestanding C -- no engine headers, no AP headers,
// no libc beyond nothing at all -- so the EXACT function the client runs can be
// compiled and asserted by the out-of-engine harness in tools/test-version-compare.c
// without dragging in the unity build. ap_hooks.c is its only in-game consumer.
//
// Shape: "A.B.C" or "A.B.C.D", with a leading "v" tolerated on either side (the
// client's CTR_AP_VERSION carries one, the apworld's world_version does not).
// A missing fourth component reads as 0, so 0.1.4.1 > 0.1.4 and 0.1.5 > 0.1.4.1
// both fall out of the plain lexicographic compare with no special cases.
//
// ANYTHING else -- a prerelease tail, a two-component version, a five-component
// version, an empty string, junk -- is UNPARSEABLE, which means "cannot tell",
// which means the comparator answers "not newer" and the notice stays silent.
// Silence is always the safe answer here: the notice is informational, and a
// wrong "update your client" is worse than no notice at all.

// Skip a leading "v"/"V" so a version can be substituted into a string that
// already prints its own "v" prefix. Never returns NULL for a non-NULL input.
static const char *AP_VersionDigits(const char *s)
{
	if (s && (s[0] == 'v' || s[0] == 'V'))
		return s + 1;
	return s;
}

// Parse "A.B.C[.D]" into out[0..3] (missing D = 0). Returns 1 on success, 0 when
// the string is not exactly that shape -- trailing text of any kind fails.
static int AP_VersionParse(const char *s, int out[4])
{
	int n = 0;

	out[0] = out[1] = out[2] = out[3] = 0;
	if (!s)
		return 0;
	s = AP_VersionDigits(s);

	while (n < 4)
	{
		int v = 0;
		int digits = 0;
		while (*s >= '0' && *s <= '9')
		{
			if (digits < 9) // ignore an absurdly long tail rather than overflow
				v = v * 10 + (*s - '0');
			digits++;
			s++;
		}
		if (digits == 0)
			return 0; // empty component ("1..2", "v.", a bare "v")
		out[n++] = v;
		if (*s != '.')
			break;
		s++;
	}

	// Must have consumed the whole string and seen at least major.minor.patch.
	return (*s == '\0' && n >= 3) ? 1 : 0;
}

// 1 when `seed` is a strictly higher pair version than `client`, else 0. An
// unparseable version on EITHER side returns 0 (cannot tell -> stay silent).
static int AP_VersionNewer(const char *seed, const char *client)
{
	int a[4];
	int b[4];
	int i;

	if (!AP_VersionParse(seed, a) || !AP_VersionParse(client, b))
		return 0;

	for (i = 0; i < 4; i++)
		if (a[i] != b[i])
			return a[i] > b[i];

	return 0; // equal
}

#endif // AP_VERSION_CMP_H
