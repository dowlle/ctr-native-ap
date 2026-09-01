#ifdef CTR_CUSTOM_TRACKS

#include <platform/native_custom_tracks.h>
#include <platform/native_sha256.h>

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>

#ifdef CTR_AP
// Bound by prototype, the way every other game-side AP call site does it
// (game/MEMPACK.c, game/231/RB_Blowup.c). Declared in ap/ap_hooks.h.
void AP_LogLine(const char *msg);
#endif

// Custom-track loader, engine half. See native_custom_tracks.h for the model
// and native_custom_tracks_policy.h for every decision this file asks.

// Every line this feature emits goes through here.
//
// It keeps writing to stdout, which is what the harnesses capture and assert
// on, AND in an AP build hands the same text to AP_LogLine, the sink that
// reaches ctr-ap.log. Before this existed the feature logged with bare printf
// to stdout only, so none of it survived a real session: a run that refused a
// track or clamped a sky left no evidence any log grep could find. Both halves
// matter -- dropping stdout would blind the harnesses, and the missing
// AP_LogLine half is what made the first runtime question unanswerable.
void CustomTrack_Log(const char *fmt, ...)
{
	char line[512];
	va_list args;

	va_start(args, fmt);
	vsnprintf(line, sizeof line, fmt, args);
	va_end(args);

	fputs(line, stdout);

#ifdef CTR_AP
	AP_LogLine(line);
#endif
}

#define CUSTOM_TRACK_PATH_MAX 512
#define CUSTOM_TRACK_HASH_CHUNK 65536

// One source file: where it is, what it must hash to, and what verifying it
// concluded. Two of these, one per role (VRM, LEV).
struct CustomTrackSource
{
	char path[CUSTOM_TRACK_PATH_MAX];
	char expectedHash[NATIVE_SHA256_HEX_BYTES];
	u32 verifiedSize;
	int verdict; // enum CustomTrackVerdict
};

static struct CustomTrackFeatureConfig s_customTrackConfig;
static struct CustomTrackSeedDescriptor s_descriptor;
static int s_haveDescriptor = 0;
static struct CustomTrackSource s_customTrackVrm;
static struct CustomTrackSource s_customTrackLev;
static int s_customTracksLoaded = 0;

// Scratch used to hand a resolved path back to the caller. The game reads track
// subfiles one at a time on a single thread, so a single buffer is fine.
static char s_customTrackResolved[CUSTOM_TRACK_PATH_MAX];

static const char *CustomTrack_VerdictText(int verdict)
{
	switch (verdict)
	{
	case CTR_CT_VERDICT_OK:
		return "ok";
	case CTR_CT_VERDICT_NO_PATH:
		return "no file configured";
	case CTR_CT_VERDICT_FILE_MISSING:
		return "file missing or empty";
	case CTR_CT_VERDICT_NO_EXPECTED:
		return "no expected sha256 configured";
	case CTR_CT_VERDICT_BAD_EXPECTED:
		return "expected sha256 is not 64 hex digits";
	case CTR_CT_VERDICT_READ_FAILED:
		return "file could not be read";
	case CTR_CT_VERDICT_HASH_MISMATCH:
		return "sha256 mismatch";
	default:
		return "unknown";
	}
}

// Put everything the seed decides back to "no custom track". The two file paths
// and the display name are NOT cleared: all three come from config.ini, are read
// once at startup, and outlive any number of seeds. Leaving the name in place
// cannot show it on a cup that is no longer displaced, because
// CustomTrackPolicy_CupDisplayName answers through the redirect predicate that
// this function just turned off.
static void CustomTrack_ResetArmedState(void)
{
	s_customTrackConfig.mappedLevelID = -1;
	s_customTrackConfig.contentVerified = 0;
	s_customTrackConfig.raceEnabled = 0;
	s_customTrackConfig.raceCupID = -1;
	s_customTrackConfig.raceLaps = 0;
	s_customTrackConfig.raceBoxes = 0;
	s_customTrackConfig.raceFieldSize = 0;

	s_customTrackVrm.expectedHash[0] = '\0';
	s_customTrackLev.expectedHash[0] = '\0';
	s_customTrackVrm.verdict = CTR_CT_VERDICT_NO_PATH;
	s_customTrackLev.verdict = CTR_CT_VERDICT_NO_PATH;
	s_customTrackVrm.verifiedSize = 0;
	s_customTrackLev.verifiedSize = 0;
}

static char *CustomTrack_Trim(char *s)
{
	while (isspace((unsigned char)*s))
		s++;
	if (*s == '\0')
		return s;
	char *end = s + strlen(s) - 1;
	while (end > s && isspace((unsigned char)*end))
		end--;
	*(end + 1) = '\0';
	return s;
}

static void CustomTrack_CopyField(char *dst, size_t dstSize, const char *src)
{
	size_t len = strlen(src);

	if (len > dstSize - 1)
		len = dstSize - 1;

	memcpy(dst, src, len);
	dst[len] = '\0';
}

// Hash a whole file and compare against its configured digest. Sets
// source->verdict and, on OK, source->verifiedSize.
static void CustomTrack_VerifySource(struct CustomTrackSource *source, const char *roleName)
{
	struct NativeSha256Ctx ctx;
	unsigned char digest[NATIVE_SHA256_DIGEST_BYTES];
	char actualHex[NATIVE_SHA256_HEX_BYTES];
	unsigned char chunk[CUSTOM_TRACK_HASH_CHUNK];
	struct stat st;
	FILE *f;
	u32 total = 0;
	size_t got;

	source->verifiedSize = 0;

	if (source->path[0] == '\0')
	{
		source->verdict = CTR_CT_VERDICT_NO_PATH;
		return;
	}

	if (source->expectedHash[0] == '\0')
	{
		source->verdict = CTR_CT_VERDICT_NO_EXPECTED;
		CustomTrack_Log("[CustomTracks] REFUSED %s \"%s\": %s\n", roleName, source->path,
                                CustomTrack_VerdictText(source->verdict));
		return;
	}

	if (strlen(source->expectedHash) != NATIVE_SHA256_DIGEST_BYTES * 2)
	{
		source->verdict = CTR_CT_VERDICT_BAD_EXPECTED;
		CustomTrack_Log("[CustomTracks] REFUSED %s \"%s\": %s\n", roleName, source->path,
                                CustomTrack_VerdictText(source->verdict));
		return;
	}

	if (stat(source->path, &st) != 0 || st.st_size <= 0)
	{
		source->verdict = CTR_CT_VERDICT_FILE_MISSING;
		CustomTrack_Log("[CustomTracks] REFUSED %s \"%s\": %s\n", roleName, source->path,
                                CustomTrack_VerdictText(source->verdict));
		return;
	}

	f = fopen(source->path, "rb");
	if (f == NULL)
	{
		source->verdict = CTR_CT_VERDICT_READ_FAILED;
		CustomTrack_Log("[CustomTracks] REFUSED %s \"%s\": %s\n", roleName, source->path,
                                CustomTrack_VerdictText(source->verdict));
		return;
	}

	NativeSha256_Init(&ctx);
	while ((got = fread(chunk, 1, sizeof(chunk), f)) > 0)
	{
		NativeSha256_Update(&ctx, chunk, got);
		total += (u32)got;
	}

	if (ferror(f))
	{
		fclose(f);
		source->verdict = CTR_CT_VERDICT_READ_FAILED;
		CustomTrack_Log("[CustomTracks] REFUSED %s \"%s\": %s\n", roleName, source->path,
                                CustomTrack_VerdictText(source->verdict));
		return;
	}
	fclose(f);

	NativeSha256_Final(&ctx, digest);
	NativeSha256_ToHex(digest, actualHex);

	if (!NativeSha256_HexEquals(source->expectedHash, actualHex))
	{
		source->verdict = CTR_CT_VERDICT_HASH_MISMATCH;
		CustomTrack_Log("[CustomTracks] REFUSED %s \"%s\": %s\n", roleName, source->path,
                                CustomTrack_VerdictText(source->verdict));
		CustomTrack_Log("[CustomTracks]   expected %s\n", source->expectedHash);
		CustomTrack_Log("[CustomTracks]   actual   %s\n", actualHex);
		return;
	}

	source->verdict = CTR_CT_VERDICT_OK;
	source->verifiedSize = total;
	CustomTrack_Log("[CustomTracks] verified %s \"%s\" (%u bytes, sha256 %s)\n", roleName, source->path,
                        (unsigned)total, actualHex);
}

void CustomTrack_Load(void)
{
	FILE *f;
	char line[1024];
	char section[64] = "";
	char navUuidText[64] = "";
	char navRevText[32] = "";

	// Idempotent: parse and verify exactly once per run.
	if (s_customTracksLoaded)
		return;
	s_customTracksLoaded = 1;

	memset(&s_customTrackConfig, 0, sizeof(s_customTrackConfig));
	memset(&s_customTrackVrm, 0, sizeof(s_customTrackVrm));
	memset(&s_customTrackLev, 0, sizeof(s_customTrackLev));

	// Disarmed until a seed hands over a descriptor. There is deliberately no
	// config.ini path to a working feature any more: no block on the wire means
	// the feature is fully off, whatever config.ini says.
	memset(&s_descriptor, 0, sizeof(s_descriptor));
	s_haveDescriptor = 0;
	CustomTrack_ResetArmedState();

	f = fopen("config.ini", "r");
	if (f == NULL)
	{
		// No config is a valid state: behave exactly like the retail build.
		CustomTrack_Log("[CustomTracks] config.ini not found, loader disarmed\n");
		return;
	}

	// Same INI shape as platform/native_config.c: [section] headers, key = value
	// lines, ';' or '#' comments. The loader's keys live in [CustomTracks].
	while (fgets(line, sizeof(line), f))
	{
		char *p = CustomTrack_Trim(line);
		char *eq;
		char *key;
		char *value;

		if (*p == '\0' || *p == ';' || *p == '#')
			continue;

		if (*p == '[')
		{
			char *end = strchr(p + 1, ']');
			if (end)
			{
				*end = '\0';
				CustomTrack_CopyField(section, sizeof(section), p + 1);
			}
			continue;
		}

		if (strcmp(section, "CustomTracks") != 0)
			continue;

		eq = strchr(p, '=');
		if (eq == NULL)
			continue;

		*eq = '\0';
		key = CustomTrack_Trim(p);
		value = CustomTrack_Trim(eq + 1);

		if (*value == '\0')
			continue;

		// config.ini carries ONLY where the two files are, and what to call the
		// track on screen. Everything else the loader needs -- both digests, the
		// lap count, the host slot, which cup is replaced, and whether boxes are
		// allowed -- arrives from slot_data, so the seed is the single authority
		// on what is SERVED and a local file cannot talk this client into racing
		// content the seed did not name.
		//
		// The name is here and not on the wire precisely because it is the one
		// thing that changes nothing about what is served. Decision 10 records
		// that a descriptor field is the proper long-term home for it. The two
		// nav-identity keys below join it for the same reason, and carry the
		// same caveat: decision 11 records that they belong in the descriptor.
		if (strcmp(key, "custom_track_vrm") == 0)
			CustomTrack_CopyField(s_customTrackVrm.path, sizeof(s_customTrackVrm.path), value);
		else if (strcmp(key, "custom_track_lev") == 0)
			CustomTrack_CopyField(s_customTrackLev.path, sizeof(s_customTrackLev.path), value);
		else if (strcmp(key, "custom_track_name") == 0)
			CustomTrack_CopyField(s_customTrackConfig.raceName, sizeof(s_customTrackConfig.raceName), value);
		else if (strcmp(key, "custom_track_nav_uuid") == 0)
			CustomTrack_CopyField(navUuidText, sizeof(navUuidText), value);
		else if (strcmp(key, "custom_track_nav_rev") == 0)
			CustomTrack_CopyField(navRevText, sizeof(navRevText), value);
	}

	fclose(f);

	// A name that cannot go on screen is dropped HERE, once, with a reason,
	// rather than being re-judged silently at every draw. The cup then shows the
	// retail name, which is exactly what a client with no name configured shows.
	if (s_customTrackConfig.raceName[0] != '\0')
	{
		const char *why = NULL;

		if (CustomTrackPolicy_NameFits(s_customTrackConfig.raceName, &why))
		{
			CustomTrack_Log("[CustomTracks] displaced cup will be called \"%s\" (%d of %d pixels at FONT_BIG)\n",
			                s_customTrackConfig.raceName,
			                CustomTrackPolicy_NameWidthPixels(s_customTrackConfig.raceName), CTR_CT_NAME_MAX_PIXELS);
		}
		else
		{
			CustomTrack_Log("[CustomTracks] custom_track_name ignored: %s; the cup keeps its retail name\n", why);
			s_customTrackConfig.raceName[0] = '\0';
		}
	}

	// Decision 11: resolve the package's recording identity, once, here, for the
	// same reason the name is resolved here -- so no later caller has to re-judge
	// the text and risk disagreeing about it.
	//
	// Every failure below lands in the same place: navIdentityValid stays 0, the
	// track is still served, and recordings simply fall back to the legacy retail
	// interpretation. A malformed identity costs the AI lines, never the race.
	if (navUuidText[0] != '\0')
	{
		if (CustomTrackPolicy_ParseNavUuid(navUuidText, s_customTrackConfig.navTrackUuid))
		{
			s_customTrackConfig.navIdentityValid = 1;
			s_customTrackConfig.navRevision = 1; // the default when no revision is configured

			if (navRevText[0] != '\0' && !CustomTrackPolicy_ParseNavRevision(navRevText, &s_customTrackConfig.navRevision))
			{
				s_customTrackConfig.navRevision = 1;
				CustomTrack_Log("[CustomTracks] custom_track_nav_rev \"%s\" is not a positive whole number; "
				                "using revision 1\n",
				                navRevText);
			}

			CustomTrack_Log("[CustomTracks] recordings identify as %s revision %u\n", navUuidText,
			                s_customTrackConfig.navRevision);
		}
		else
		{
			CustomTrack_Log("[CustomTracks] custom_track_nav_uuid \"%s\" is not a canonical 8-4-4-4-12 UUID; "
			                "this build stamps and matches no custom-track recording identity\n",
			                navUuidText);
		}
	}
	else if (navRevText[0] != '\0')
	{
		CustomTrack_Log("[CustomTracks] custom_track_nav_rev is set but custom_track_nav_uuid is not; "
		                "a revision alone identifies nothing and is ignored\n");
	}

	if (s_customTrackVrm.path[0] == '\0' && s_customTrackLev.path[0] == '\0')
	{
		CustomTrack_Log("[CustomTracks] no track files configured; the feature stays off\n");
		return;
	}

	CustomTrack_Log("[CustomTracks] track files configured; awaiting a seed descriptor\n");
	CustomTrack_Log("[CustomTracks]   vrm \"%s\"\n", s_customTrackVrm.path);
	CustomTrack_Log("[CustomTracks]   lev \"%s\"\n", s_customTrackLev.path);
}

void CustomTrack_ClearSeedDescriptor(void)
{
	if (!s_customTracksLoaded)
		CustomTrack_Load();

	if (!s_haveDescriptor)
		return;

	// A seed that goes away takes the whole feature with it. Nothing is served
	// and every cup is back to its vanilla legs on the very next read.
	CustomTrack_Log("[CustomTracks] seed descriptor withdrawn; the feature is off\n");
	memset(&s_descriptor, 0, sizeof(s_descriptor));
	s_haveDescriptor = 0;
	CustomTrack_ResetArmedState();
}

int CustomTrack_UseManagedPackage(const struct CustomTrackManagerPackage *package,
	                              const struct CustomTrackManagerStatus *status)
{
	unsigned char uuid[CTR_CT_NAV_UUID_BYTES];

	if (!s_customTracksLoaded)
		CustomTrack_Load();
	if (package == NULL || status == NULL || status->state != CTR_CT_MANAGER_READY ||
	    status->levPath[0] == '\0' || status->vrmPath[0] == '\0' ||
	    strlen(status->levPath) >= sizeof s_customTrackLev.path ||
	    strlen(status->vrmPath) >= sizeof s_customTrackVrm.path ||
	    !CustomTrackPolicy_NameFits(package->title, NULL) ||
	    !CustomTrackPolicy_ParseNavUuid(package->navigationUuid, uuid) ||
	    package->navigationRevision == 0)
	{
		CustomTrack_Log("[CustomTracks] REFUSED manager package: invalid or not Ready\n");
		return 0;
	}

	CustomTrack_CopyField(s_customTrackLev.path, sizeof s_customTrackLev.path, status->levPath);
	CustomTrack_CopyField(s_customTrackVrm.path, sizeof s_customTrackVrm.path, status->vrmPath);
	CustomTrack_CopyField(s_customTrackConfig.raceName, sizeof s_customTrackConfig.raceName, package->title);
	memcpy(s_customTrackConfig.navTrackUuid, uuid, sizeof uuid);
	s_customTrackConfig.navRevision = package->navigationRevision;
	s_customTrackConfig.navIdentityValid = 1;

	// A path or package handoff must invalidate the descriptor memcmp cache. The
	// next ApplySeedDescriptor owns the full digest check before it can arm.
	memset(&s_descriptor, 0, sizeof s_descriptor);
	s_haveDescriptor = 0;
	CustomTrack_ResetArmedState();
	CustomTrack_Log("[CustomTracks] manager selected %s %s from \"%s\"\n",
	                package->title, package->version, status->packageRoot);
	return 1;
}

int CustomTrack_ReverifyArmedContent(void)
{
	if (!s_customTracksLoaded)
		CustomTrack_Load();
	if (!s_haveDescriptor || !s_customTrackConfig.contentVerified)
		return 0;

	CustomTrack_VerifySource(&s_customTrackVrm, "vrm preflight");
	CustomTrack_VerifySource(&s_customTrackLev, "lev preflight");
	if (s_customTrackVrm.verdict != CTR_CT_VERDICT_OK ||
	    s_customTrackLev.verdict != CTR_CT_VERDICT_OK)
	{
		CustomTrack_Log("[CustomTracks] event-entry preflight failed; custom race disarmed\n");
		s_customTrackConfig.contentVerified = 0;
		s_customTrackConfig.raceEnabled = 0;
		s_customTrackConfig.mappedLevelID = -1;
		return 0;
	}
	return 1;
}

int CustomTrack_ApplySeedDescriptor(const struct CustomTrackSeedDescriptor *d)
{
	const char *why = NULL;
	int cupID;

	if (!s_customTracksLoaded)
		CustomTrack_Load();

	if (d == NULL)
	{
		CustomTrack_ClearSeedDescriptor();
		return 0;
	}

	// Idempotent by content. This is called every frame from AP_OnFrame rather
	// than from a connect callback, because the parse has no hook of its own;
	// re-hashing a multi-MiB track 60 times a second is not an option, so an
	// unchanged descriptor must cost a memcmp and nothing else.
	if (s_haveDescriptor && memcmp(&s_descriptor, d, sizeof(s_descriptor)) == 0)
		return s_customTrackConfig.contentVerified;

	s_descriptor = *d;
	s_haveDescriptor = 1;
	CustomTrack_ResetArmedState();

	cupID = d->replacesCupLevelID - 100;

	CustomTrack_Log("[CustomTracks] seed descriptor: cup LevelID %d -> single %d-lap race on host "
                        "slot %d (boxes %s)\n",
                        d->replacesCupLevelID, d->laps, d->hostLevelID, d->boxes ? "allowed" : "denied");

	// Wire-shape validation already ran in ap_seedcfg; this is the engine's own
	// authority over what it can actually serve, and it is deliberately separate.
	// A value that is well-formed on the wire can still be one this build cannot
	// honour, and refusing here is what keeps a plausible descriptor from
	// producing a wrong-content race.
	if (!CustomTrackPolicy_LevelIDIsMappable(d->hostLevelID))
	{
		CustomTrack_Log("[CustomTracks] REFUSED: host slot %d is not an arcade slot 0..%d\n",
                                d->hostLevelID, CTR_CT_MAX_LEVELS - 1);
		return 0;
	}

	if (cupID < 0 || cupID > 4)
	{
		CustomTrack_Log("[CustomTracks] REFUSED: cup LevelID %d is not a Gem Cup 100..104\n",
                                d->replacesCupLevelID);
		return 0;
	}

	if (d->laps < 1 || d->laps > 7)
	{
		CustomTrack_Log("[CustomTracks] REFUSED: %d laps is outside 1..7\n", d->laps);
		return 0;
	}

	if (!CustomTrackPolicy_FlagsSupportRace(d->flagAiNav, d->flagSpawns, cupID, &why))
	{
		CustomTrack_Log("[CustomTracks] REFUSED: %s (ai_nav=%d spawns=%d, this cup's grid needs %d)\n",
                                why, d->flagAiNav, d->flagSpawns, CustomTrackPolicy_RequiredSpawns(cupID));
		return 0;
	}

	if (s_customTrackVrm.path[0] == '\0' || s_customTrackLev.path[0] == '\0')
	{
		// The seed names a track this client has no files for. Loud, because the
		// player CAN fix it, and total, because the alternative is racing the
		// host slot's retail track for a Gem the seed thinks is on Baby T Park.
		CustomTrack_Log("[CustomTracks] REFUSED: this seed binds a custom track, but config.ini "
                                "names no custom_track_lev/custom_track_vrm files for it\n");
		CustomTrack_Log("[CustomTracks] the cup stays vanilla; add the track files to play this seed\n");
		return 0;
	}

	// The seed is the authority on content: its digests, not config.ini's.
	CustomTrack_CopyField(s_customTrackLev.expectedHash, sizeof(s_customTrackLev.expectedHash),
	                      d->levSha256);
	CustomTrack_CopyField(s_customTrackVrm.expectedHash, sizeof(s_customTrackVrm.expectedHash),
	                      d->vrmSha256);

	s_customTrackConfig.mappedLevelID = d->hostLevelID;
	s_customTrackConfig.raceCupID = cupID;
	s_customTrackConfig.raceLaps = d->laps;
	s_customTrackConfig.raceBoxes = d->boxes ? 1 : 0;

	// The field follows the track. Derived once, here, so the driver count, the
	// standings layout and the harnesses all read one number instead of each
	// re-clamping the raw spawn count.
	s_customTrackConfig.raceFieldSize = CustomTrackPolicy_FieldSizeForSpawns(d->flagSpawns);
	s_customTrackConfig.raceEnabled = 1;

	CustomTrack_Log("[CustomTracks] event race grids %d karts (track reports %d spawn slots, clamped to %d..%d)\n",
	                s_customTrackConfig.raceFieldSize, d->flagSpawns, CTR_CT_FIELD_MIN, CTR_CT_FIELD_MAX);

	CustomTrack_VerifySource(&s_customTrackVrm, "vrm");
	CustomTrack_VerifySource(&s_customTrackLev, "lev");

	if (s_customTrackVrm.verdict != CTR_CT_VERDICT_OK || s_customTrackLev.verdict != CTR_CT_VERDICT_OK)
	{
		// Loud and total: no bytes served AND the cup stays vanilla. Serving
		// retail bytes for a race the seed thinks is the custom track is exactly
		// the silent wrong-content outcome the digests exist to prevent.
		CustomTrack_Log("[CustomTracks] DISARMED: content did not match the seed's digests "
                                "(vrm: %s, lev: %s)\n",
                                CustomTrack_VerdictText(s_customTrackVrm.verdict),
                                CustomTrack_VerdictText(s_customTrackLev.verdict));
		CustomTrack_Log("[CustomTracks] the cup stays vanilla and no custom bytes are served\n");
		s_customTrackConfig.raceEnabled = 0;
		s_customTrackConfig.mappedLevelID = -1;
		return 0;
	}

	s_customTrackConfig.contentVerified = 1;
	CustomTrack_Log("[CustomTracks] armed: cup %d becomes a single %d-lap race on host slot %d\n",
                        cupID, d->laps, d->hostLevelID);
	CustomTrack_Log("[CustomTracks] host slot %d serves custom bytes ONLY for that race; its retail "
                        "race pad still loads retail bytes\n",
                        d->hostLevelID);
	return 1;
}

const struct CustomTrackFeatureConfig *CustomTrack_Config(void)
{
	if (!s_customTracksLoaded)
		CustomTrack_Load();

	return &s_customTrackConfig;
}

int CustomTrack_GetOverride(int subfileIndex, const struct CustomTrackLoadContext *ctx, const char **outPath, u32 *outSize)
{
	const struct CustomTrackSource *source;
	struct stat st;
	int role;

	if (!s_customTracksLoaded)
		CustomTrack_Load();

	// Cheapest terms first: this runs on EVERY BIGFILE read in the game, and for
	// all but the event race's own two subfiles it must fall out immediately.
	if (!s_customTrackConfig.contentVerified)
		return 0;

	role = CustomTrackPolicy_SubfileRole(subfileIndex, s_customTrackConfig.mappedLevelID);
	if (role == CTR_CT_ROLE_NONE)
		return 0;

	// The index is in the mapped group, but that alone does not make this read
	// the event race's -- the host slot's own retail race reads the same eight
	// indices. Only the load context can tell them apart, and it is what keeps
	// that retail race retail.
	if (!CustomTrackPolicy_ShouldServe(&s_customTrackConfig, ctx))
		return 0;

	source = (role == CTR_CT_ROLE_LEV) ? &s_customTrackLev : &s_customTrackVrm;

	// The content was hashed at startup. Re-hashing on every subfile read would
	// cost a multi-MiB digest inside the load path, so the serve-time check is
	// the cheap half: the file must still be exactly the size that was verified.
	// That catches the realistic accident -- a file replaced or truncated while
	// the game is running -- without turning every level load into a hash.
	if (stat(source->path, &st) != 0 || (u32)st.st_size != source->verifiedSize)
	{
		CustomTrack_Log("[CustomTracks] REFUSED subfile %d: \"%s\" changed since it was verified, "
                                "falling back to BIGFILE\n",
                                subfileIndex, source->path);
		return 0;
	}

	CustomTrack_CopyField(s_customTrackResolved, sizeof(s_customTrackResolved), source->path);

	if (outPath)
		*outPath = s_customTrackResolved;
	if (outSize)
		*outSize = source->verifiedSize;

	CustomTrack_Log("[CustomTracks] serving subfile %d (%s) from \"%s\" (%u bytes)\n", subfileIndex,
                        (role == CTR_CT_ROLE_LEV) ? "lev" : "vrm", s_customTrackResolved,
                        (unsigned)source->verifiedSize);
	return 1;
}

int CustomTrack_ReadFile(const char *path, void *dst, u32 bufBytes, u32 fileBytes)
{
	FILE *f;
	size_t got;

	if (path == NULL || dst == NULL)
		return 0;

	if (fileBytes > bufBytes)
		fileBytes = bufBytes; // defensive: never write past the caller's buffer

	f = fopen(path, "rb");
	if (f == NULL)
	{
		CustomTrack_Log("[CustomTracks] ERROR: could not reopen \"%s\"\n", path);
		return 0;
	}

	got = fread(dst, 1, (size_t)fileBytes, f);
	fclose(f);

	if (got != (size_t)fileBytes)
	{
		CustomTrack_Log("[CustomTracks] ERROR: short read on \"%s\" (%u of %u bytes)\n", path, (unsigned)got,
                                (unsigned)fileBytes);
		return 0;
	}

	// Zero the sector-round padding tail, matching a BIGFILE sector read whose
	// trailing padding is not part of the subfile's payload.
	if (bufBytes > fileBytes)
		memset((char *)dst + fileBytes, 0, (size_t)(bufBytes - fileBytes));

	return 1;
}

int CustomTrack_CupRaceRedirectActive(int cupID, int isAdventureCup)
{
	return CustomTrackPolicy_ShouldRedirectCup(CustomTrack_Config(), cupID, isAdventureCup);
}

int CustomTrack_CupRaceLevelID(int cupID, int isAdventureCup)
{
	return CustomTrackPolicy_RaceLevelID(CustomTrack_Config(), cupID, isAdventureCup);
}

int CustomTrack_CupRaceLaps(int cupID, int isAdventureCup)
{
	return CustomTrackPolicy_RaceLaps(CustomTrack_Config(), cupID, isAdventureCup);
}

int CustomTrack_CupIsComplete(int cupID, int isAdventureCup, int trackIndexAfterIncrement)
{
	return CustomTrackPolicy_CupIsComplete(CustomTrack_Config(), cupID, isAdventureCup, trackIndexAfterIncrement);
}

int CustomTrack_CupLegCount(int cupID, int isAdventureCup)
{
	return CustomTrackPolicy_CupLegCount(CustomTrack_Config(), cupID, isAdventureCup);
}

const char *CustomTrack_CupDisplayName(int cupID, int isAdventureCup)
{
	return CustomTrackPolicy_CupDisplayName(CustomTrack_Config(), cupID, isAdventureCup);
}

int CustomTrack_BoxVerdict(int levelID, int adventureCupActive, int cupID)
{
	struct CustomTrackLoadContext ctx;

	ctx.levelID = levelID;
	ctx.adventureCupActive = adventureCupActive;
	ctx.cupID = cupID;

	return CustomTrackPolicy_BoxVerdict(CustomTrack_Config(), &ctx);
}

int CustomTrack_EventFieldSize(int levelID, int adventureCupActive, int cupID)
{
	struct CustomTrackLoadContext ctx;

	ctx.levelID = levelID;
	ctx.adventureCupActive = adventureCupActive;
	ctx.cupID = cupID;

	return CustomTrackPolicy_EventFieldSize(CustomTrack_Config(), &ctx);
}

int CustomTrack_ServingLoad(int levelID, int adventureCupActive, int cupID)
{
	struct CustomTrackLoadContext ctx;

	ctx.levelID = levelID;
	ctx.adventureCupActive = adventureCupActive;
	ctx.cupID = cupID;

	return CustomTrackPolicy_ShouldServe(CustomTrack_Config(), &ctx);
}

int CustomTrack_RetailPodiumLevelID(int levelID, int adventureCupActive, int cupID)
{
	struct CustomTrackLoadContext ctx;

	ctx.levelID = levelID;
	ctx.adventureCupActive = adventureCupActive;
	ctx.cupID = cupID;

	return CustomTrackPolicy_RetailPodiumLevelID(CustomTrack_Config(), &ctx);
}

int CustomTrack_NavIdentityForLoad(int levelID, int adventureCupActive, int cupID, unsigned char outUuid[CTR_CT_NAV_UUID_BYTES],
                                   unsigned int *outRevision)
{
	const struct CustomTrackFeatureConfig *cfg;
	int                                    i;

	if (outUuid == NULL || outRevision == NULL)
		return 0;

	// The identity answers for exactly the loads the BYTES answer for. Asking
	// the same predicate here is what keeps a recording's identity and the
	// geometry it was recorded against from ever disagreeing: if this load is
	// not serving the custom track, it is a retail load and carries the retail
	// identity, whatever config.ini holds.
	if (!CustomTrack_ServingLoad(levelID, adventureCupActive, cupID))
		return 0;

	cfg = CustomTrack_Config();
	if (!cfg->navIdentityValid)
		return 0;

	for (i = 0; i < CTR_CT_NAV_UUID_BYTES; i++)
		outUuid[i] = cfg->navTrackUuid[i];
	*outRevision = cfg->navRevision;
	return 1;
}

int CustomTrack_RaceFeatureEnabled(void)
{
	const struct CustomTrackFeatureConfig *cfg = CustomTrack_Config();

	return (cfg->raceEnabled && cfg->contentVerified) ? 1 : 0;
}

// ---------------------------------------------------------------------------
// Per-load render accounting. See native_custom_tracks.h for why the rung-1
// high-water line could not answer the question it was built for.

struct CustomTrackDiagLoad
{
	int levelID;
	int active;    // a frame has been seen for this levelID
	int frameOpen; // between BeginFrame and NoteFrameSpend, which is the only
	               // window whose drops belong to this load. Split-screen never
	               // opens it, so a 3P race cannot leave its refusals in the
	               // report of the 1P load before it.

	unsigned long capacityBytes;
	unsigned long frames;

	// The frame with the largest post-sky spend, and its breakdown.
	unsigned long worstSpend;
	unsigned long worstBeforeGeom;
	unsigned long worstAfterGeom;
	int worstPrimCount;
	int worstLeavesDrawn;

	// Events a completed frame cannot show.
	unsigned long reserveRefusals;
	unsigned long tightestReserve; // what the bucket wanted, on the closest refusal
	unsigned long tightestFree;    // what was left when it wanted it
	unsigned long renderedListFull;
	unsigned long bspRecordsDropped;
};

static struct CustomTrackDiagLoad sCustomTrackDiag;

static void CustomTrackDiag_Begin(int levelID, unsigned long capacityBytes)
{
	memset(&sCustomTrackDiag, 0, sizeof sCustomTrackDiag);
	sCustomTrackDiag.levelID = levelID;
	sCustomTrackDiag.capacityBytes = capacityBytes;
	sCustomTrackDiag.active = 1;
}

void CustomTrackDiag_FlushLevelLoad(void)
{
	struct CustomTrackDiagLoad *d = &sCustomTrackDiag;
	unsigned long freeBytes;

	if (!d->active || d->frames == 0)
	{
		d->active = 0;
		return;
	}

	freeBytes = (d->capacityBytes > d->worstSpend) ? (d->capacityBytes - d->worstSpend) : 0;

	CustomTrack_Log("[CustomTracks] level %d over %lu frames: prim arena worst frame %lu/%lu bytes "
	                "(other draws %lu, terrain +%lu, sky +%lu, %lu free, %d primitives across %d leaves); "
	                "reserve refused %lu, rendered list full %lu, bsp records dropped %lu\n",
	                d->levelID, d->frames, d->worstSpend, d->capacityBytes, d->worstBeforeGeom,
	                d->worstAfterGeom - d->worstBeforeGeom, d->worstSpend - d->worstAfterGeom, freeBytes, d->worstPrimCount,
	                d->worstLeavesDrawn, d->reserveRefusals, d->renderedListFull, d->bspRecordsDropped);

	if (d->reserveRefusals != 0)
	{
		// Loud and separate, because this is the prefix cut itself rather than a
		// figure that hints at it: the frame stopped drawing level geometry
		// partway through and told the player nothing.
		CustomTrack_Log("[CustomTracks] level %d LOST LEVEL GEOMETRY: a bucket reserve was refused %lu times across %lu "
		                "frames, closest %lu bytes wanted with %lu left, and DrawLevelOvr1P abandoned the rest of each "
		                "of those frames\n",
		                d->levelID, d->reserveRefusals, d->frames, d->tightestReserve, d->tightestFree);
	}

	if (d->renderedListFull != 0)
	{
		CustomTrack_Log("[CustomTracks] level %d rendered-quadblock list hit the end of its %lu slots %lu times; "
		                "those appends were refused rather than written past the array\n",
		                d->levelID, CTR_CT_RENDERED_QUADBLOCK_SLOTS, d->renderedListFull);
	}

	d->active = 0;
}

void CustomTrackDiag_BeginFrame(int levelID, unsigned long capacityBytes)
{
	struct CustomTrackDiagLoad *d = &sCustomTrackDiag;

	// levelID keys the load rather than the arena size rung 1 used, because two
	// different levels can share a budget and one level can be loaded twice in a
	// session -- both of which merged into a single report before. Capacity is
	// still part of the key so a same-level reload with a different arena starts
	// a fresh report.
	if (!d->active || d->levelID != levelID || d->capacityBytes != capacityBytes)
	{
		CustomTrackDiag_FlushLevelLoad();
		CustomTrackDiag_Begin(levelID, capacityBytes);
	}

	d->frames++;
	d->frameOpen = 1;
}

void CustomTrackDiag_NoteFrameSpend(unsigned long beforeGeomBytes, unsigned long afterGeomBytes, unsigned long afterSkyBytes, int primCount,
                                    int leavesDrawn)
{
	struct CustomTrackDiagLoad *d = &sCustomTrackDiag;

	d->frameOpen = 0;

	if (!d->active || afterSkyBytes <= d->worstSpend)
		return;

	d->worstSpend = afterSkyBytes;
	d->worstBeforeGeom = beforeGeomBytes;
	d->worstAfterGeom = afterGeomBytes;
	d->worstPrimCount = primCount;
	d->worstLeavesDrawn = leavesDrawn;
}

void CustomTrackDiag_NoteReserveRefused(unsigned long reserveBytes, unsigned long freeBytes)
{
	struct CustomTrackDiagLoad *d = &sCustomTrackDiag;

	if (!d->frameOpen)
		return;

	d->reserveRefusals++;

	// Keep the refusal that came closest to fitting. The smallest shortfall is
	// the one that says how much headroom this load actually needed, and it is
	// the figure a later sizing decision would be based on.
	if (d->reserveRefusals == 1 || (reserveBytes - freeBytes) < (d->tightestReserve - d->tightestFree))
	{
		d->tightestReserve = reserveBytes;
		d->tightestFree = freeBytes;
	}
}

void CustomTrackDiag_NoteRenderedListFull(void)
{
	if (sCustomTrackDiag.frameOpen)
		sCustomTrackDiag.renderedListFull++;
}

void CustomTrackDiag_NoteBspRecordDropped(void)
{
	if (sCustomTrackDiag.frameOpen)
		sCustomTrackDiag.bspRecordsDropped++;
}

#endif // CTR_CUSTOM_TRACKS
