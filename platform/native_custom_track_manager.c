#ifdef CTR_CUSTOM_TRACKS

#include <platform/native_custom_track_manager.h>

#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32)
#include <direct.h>
#include <platform/native_win32.h>
#else
#include <dirent.h>
#endif

#define CTR_CT_MANAGER_HASH_CHUNK 65536
#define CTR_CT_MANAGER_TEXT_MAX 4096

static const struct CustomTrackManagerPackage s_babyTParkPackage = {
	"baby-t-park",
	"60d5a8a8-b69a-4f6a-a0d8-9a43d91e3f2e",
	"1.0.0",
	"Baby T Park",
	"Lockheart",
	"https://www.projectsaphi.com/tracks/101",
	"https://www.projectsaphi.com/api/v2/tracks/101/downloads",
	"0.2.0-alpha6",
	"0.2.0-alpha6",
	"96ad9f74f51a02eafcc207cd02c97052d674c950e0f24b6440a227494a705fe8",
	"2dcaa0fe93359c7ae00fb93842a581210e0dcc2db73f4de43508375834092e83",
	"898a9315-693f-4ed3-b6a0-fbe50db8bc40",
	1,
	7,
	1, 1, 1, 1, 0, 0, 8, 35
};

static void Manager_SetDetail(struct CustomTrackManagerStatus *status, int state,
	                          const char *fmt, ...)
{
	va_list args;

	status->state = state;
	va_start(args, fmt);
	vsnprintf(status->detail, sizeof status->detail, fmt, args);
	va_end(args);
}

static int Manager_Join(char *dst, size_t dstSize, const char *left, const char *right)
{
	int wrote;
	size_t leftLen;

	if (dst == NULL || dstSize == 0 || left == NULL || right == NULL || left[0] == '\0' || right[0] == '\0')
		return 0;

	leftLen = strlen(left);
	wrote = snprintf(dst, dstSize, (left[leftLen - 1] == '/' || left[leftLen - 1] == '\\') ? "%s%s" : "%s/%s",
	                 left, right);
	return wrote >= 0 && (size_t)wrote < dstSize;
}

static int Manager_IsHex64(const char *s)
{
	int i;
	if (s == NULL)
		return 0;
	for (i = 0; i < 64; i++)
	{
		char c = s[i];
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
			return 0;
	}
	return s[64] == '\0';
}

static int Manager_IsUuid(const char *s)
{
	int i;
	if (s == NULL || strlen(s) != 36)
		return 0;
	for (i = 0; i < 36; i++)
	{
		char c = s[i];
		if (i == 8 || i == 13 || i == 18 || i == 23)
		{
			if (c != '-')
				return 0;
		}
		else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
			return 0;
	}
	return 1;
}

static int Manager_IsSafeId(const char *s)
{
	const unsigned char *p = (const unsigned char *)s;
	if (p == NULL || *p == '\0')
		return 0;
	for (; *p != '\0'; p++)
		if (!( (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '-'))
			return 0;
	return 1;
}

// The Alpha6 registry is compiled, not user-authored. Still reject characters
// that would require JSON/YAML escaping so a future registry edit cannot turn
// a metadata string into generated syntax.
static int Manager_IsPlainText(const char *s)
{
	const unsigned char *p = (const unsigned char *)s;
	if (p == NULL || *p == '\0')
		return 0;
	for (; *p != '\0'; p++)
		if (*p < 0x20 || *p == '"' || *p == '\\' || *p == '\r' || *p == '\n')
			return 0;
	return 1;
}

static int Manager_PackageSupported(const struct CustomTrackManagerPackage *package)
{
	return package != NULL &&
	       Manager_IsSafeId(package->id) &&
	       Manager_IsUuid(package->packageUuid) &&
	       Manager_IsUuid(package->navigationUuid) &&
	       Manager_IsHex64(package->levSha256) &&
	       Manager_IsHex64(package->vrmSha256) &&
	       Manager_IsPlainText(package->version) &&
	       Manager_IsPlainText(package->title) &&
	       Manager_IsPlainText(package->author) &&
	       Manager_IsPlainText(package->sourceUrl) &&
	       Manager_IsPlainText(package->downloadApiUrl) &&
	       Manager_IsPlainText(package->minimumClientVersion) &&
	       Manager_IsPlainText(package->minimumApworldVersion) &&
	       package->navigationRevision > 0 &&
	       package->laps >= 1 && package->laps <= 7 &&
	       (package->flagCrates == 0 || package->flagCrates == 1) &&
	       (package->flagCtrLetters == 0 || package->flagCtrLetters == 1) &&
	       (package->flagRelicCrates == 0 || package->flagRelicCrates == 1) &&
	       (package->flagAiNav == 0 || package->flagAiNav == 1) &&
	       (package->flagMinimap == 0 || package->flagMinimap == 1) &&
	       (package->flagGhosts == 0 || package->flagGhosts == 1) &&
	       package->flagSpawns >= 1 && package->flagSpawns <= 8 &&
	       package->flagCheckpoints >= 1 && package->flagCheckpoints <= 255;
}

static int Manager_BuildPaths(const char *assetsRoot,
	                          const struct CustomTrackManagerPackage *package,
	                          struct CustomTrackManagerStatus *status)
{
	char tracks[CTR_CT_MANAGER_PATH_MAX];
	char original[CTR_CT_MANAGER_PATH_MAX];

	memset(status, 0, sizeof *status);
	status->state = CTR_CT_MANAGER_IO_ERROR;

	if (!Manager_Join(tracks, sizeof tracks, assetsRoot, "tracks") ||
	    !Manager_Join(status->packageRoot, sizeof status->packageRoot, tracks, package->id) ||
	    !Manager_Join(original, sizeof original, status->packageRoot, "original") ||
	    !Manager_Join(status->levPath, sizeof status->levPath, original, "track.lev") ||
	    !Manager_Join(status->vrmPath, sizeof status->vrmPath, original, "track.vrm") ||
	    !Manager_Join(status->manifestPath, sizeof status->manifestPath, status->packageRoot, "manifest.json") ||
	    !Manager_Join(status->yamlPath, sizeof status->yamlPath, tracks, "custom_tracks.generated.yaml"))
	{
		Manager_SetDetail(status, CTR_CT_MANAGER_IO_ERROR, "Installed-content path is empty or too long.");
		return 0;
	}
	return 1;
}

static int Manager_DirectoryExists(const char *path)
{
	struct stat st;
	return stat(path, &st) == 0 && (st.st_mode & S_IFDIR) != 0;
}

static int Manager_CreateDirectory(const char *path)
{
	if (Manager_DirectoryExists(path))
		return 1;
#if defined(_WIN32)
	if (_mkdir(path) == 0)
		return 1;
#else
	if (mkdir(path, 0755) == 0)
		return 1;
#endif
	return errno == EEXIST && Manager_DirectoryExists(path);
}

static int Manager_HashFile(const char *path, char outHex[NATIVE_SHA256_HEX_BYTES],
	                        unsigned long *outBytes, int *outMissing)
{
	struct NativeSha256Ctx ctx;
	unsigned char digest[NATIVE_SHA256_DIGEST_BYTES];
	unsigned char chunk[CTR_CT_MANAGER_HASH_CHUNK];
	struct stat st;
	FILE *file;
	size_t got;
	unsigned long total = 0;

	outHex[0] = '\0';
	*outBytes = 0;
	*outMissing = 0;

	if (stat(path, &st) != 0 || st.st_size <= 0)
	{
		*outMissing = 1;
		return 0;
	}

	file = fopen(path, "rb");
	if (file == NULL)
		return 0;

	NativeSha256_Init(&ctx);
	while ((got = fread(chunk, 1, sizeof chunk, file)) > 0)
	{
		NativeSha256_Update(&ctx, chunk, got);
		total += (unsigned long)got;
	}
	if (ferror(file))
	{
		fclose(file);
		return 0;
	}
	fclose(file);

	NativeSha256_Final(&ctx, digest);
	NativeSha256_ToHex(digest, outHex);
	*outBytes = total;
	return 1;
}

static int Manager_AsciiLower(int c)
{
	return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

static int Manager_HasExtension(const char *name, const char *extension)
{
	size_t nameLen;
	size_t extLen;
	size_t i;

	if (name == NULL || extension == NULL)
		return 0;
	nameLen = strlen(name);
	extLen = strlen(extension);
	if (nameLen <= extLen)
		return 0;
	for (i = 0; i < extLen; i++)
		if (Manager_AsciiLower((unsigned char)name[nameLen - extLen + i]) !=
		    Manager_AsciiLower((unsigned char)extension[i]))
			return 0;
	return 1;
}

static int Manager_IsSafeFileName(const char *name)
{
	const unsigned char *p = (const unsigned char *)name;
	if (p == NULL || *p == '\0' || !strcmp(name, ".") || !strcmp(name, ".."))
		return 0;
	for (; *p != '\0'; p++)
		if (!( (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
		       (*p >= '0' && *p <= '9') || *p == '-' || *p == '_' || *p == '.'))
			return 0;
	return 1;
}

static const char *Manager_BaseName(const char *path)
{
	const char *slash;
	const char *backslash;
	if (path == NULL)
		return "";
	slash = strrchr(path, '/');
	backslash = strrchr(path, '\\');
	if (backslash != NULL && (slash == NULL || backslash > slash))
		slash = backslash;
	return slash == NULL ? path : slash + 1;
}

static int Manager_ConsiderSource(const char *original, const char *name,
	                              const char *extension, const char *expectedHash,
	                              char *bestPath, char bestHash[NATIVE_SHA256_HEX_BYTES],
	                              unsigned long *bestBytes, int *outCandidates,
	                              int *outUnreadable)
{
	char path[CTR_CT_MANAGER_PATH_MAX];
	char hash[NATIVE_SHA256_HEX_BYTES];
	unsigned long bytes;
	int missing;

	if (!Manager_IsSafeFileName(name) || !Manager_HasExtension(name, extension))
		return 1;
	(*outCandidates)++;
	if (!Manager_Join(path, sizeof path, original, name))
		return 0;
	if (!Manager_HashFile(path, hash, &bytes, &missing))
	{
		if (!missing)
			*outUnreadable = 1;
		return 1;
	}
	if (!NativeSha256_HexEquals(expectedHash, hash))
		return 1;
	if (bestPath[0] == '\0' || strcmp(name, Manager_BaseName(bestPath)) < 0)
	{
		snprintf(bestPath, CTR_CT_MANAGER_PATH_MAX, "%s", path);
		snprintf(bestHash, NATIVE_SHA256_HEX_BYTES, "%s", hash);
		*bestBytes = bytes;
	}
	return 1;
}

static int Manager_FindVerifiedSource(const char *original, const char *extension,
	                                  const char *expectedHash, char *bestPath,
	                                  char bestHash[NATIVE_SHA256_HEX_BYTES],
	                                  unsigned long *bestBytes, int *outCandidates,
	                                  int *outUnreadable)
{
	bestPath[0] = '\0';
	bestHash[0] = '\0';
	*bestBytes = 0;
	*outCandidates = 0;
	*outUnreadable = 0;
#if defined(_WIN32)
	{
		char pattern[CTR_CT_MANAGER_PATH_MAX];
		WIN32_FIND_DATAA data;
		HANDLE find;
		if (!Manager_Join(pattern, sizeof pattern, original, "*"))
			return 0;
		find = FindFirstFileA(pattern, &data);
		if (find == INVALID_HANDLE_VALUE)
			return GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND;
		do
		{
			if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 &&
			    !Manager_ConsiderSource(original, data.cFileName, extension, expectedHash,
			                            bestPath, bestHash, bestBytes, outCandidates, outUnreadable))
			{
				FindClose(find);
				return 0;
			}
		} while (FindNextFileA(find, &data));
		FindClose(find);
	}
#else
	{
		DIR *dir = opendir(original);
		struct dirent *entry;
		if (dir == NULL)
			return errno == ENOENT;
		errno = 0;
		while ((entry = readdir(dir)) != NULL)
			if (!Manager_ConsiderSource(original, entry->d_name, extension, expectedHash,
			                            bestPath, bestHash, bestBytes, outCandidates, outUnreadable))
			{
				closedir(dir);
				return 0;
			}
		if (errno != 0)
		{
			closedir(dir);
			return 0;
		}
		closedir(dir);
	}
#endif
	return 1;
}

static int Manager_RenderManifest(const struct CustomTrackManagerPackage *package,
	                              const struct CustomTrackManagerStatus *status,
	                              char *dst, size_t dstSize)
{
	const char *levName = Manager_BaseName(status->levPath);
	const char *vrmName = Manager_BaseName(status->vrmPath);
	if (!Manager_IsSafeFileName(levName) || !Manager_IsSafeFileName(vrmName))
		return 0;
	int wrote = snprintf(dst, dstSize,
		"{\n"
		"  \"schema_version\": 1,\n"
		"  \"id\": \"%s\",\n"
		"  \"package_uuid\": \"%s\",\n"
		"  \"version\": \"%s\",\n"
		"  \"title\": \"%s\",\n"
		"  \"authors\": [\"%s\"],\n"
		"  \"contributors\": [],\n"
		"  \"source_url\": \"%s\",\n"
		"  \"license\": null,\n"
		"  \"credits\": [],\n"
		"  \"minimum_client_version\": \"%s\",\n"
		"  \"minimum_apworld_version\": \"%s\",\n"
		"  \"files\": {\n"
		"    \"lev\": {\"path\": \"original/%s\", \"sha256\": \"%s\"},\n"
		"    \"vrm\": {\"path\": \"original/%s\", \"sha256\": \"%s\"}\n"
		"  },\n"
		"  \"navigation\": {\"uuid\": \"%s\", \"revision\": %u},\n"
		"  \"capabilities\": {\n"
		"    \"crates\": %s,\n"
		"    \"ctr_letters\": %s,\n"
		"    \"relic_crates\": %s,\n"
		"    \"ai_nav\": %s,\n"
		"    \"minimap\": %s,\n"
		"    \"ghosts\": %s,\n"
		"    \"spawns\": %d,\n"
		"    \"checkpoints\": %d,\n"
		"    \"ap_boxes\": false\n"
		"  }\n"
		"}\n",
		package->id, package->packageUuid, package->version, package->title,
		package->author, package->sourceUrl, package->minimumClientVersion,
		package->minimumApworldVersion, levName, package->levSha256, vrmName, package->vrmSha256,
		package->navigationUuid, package->navigationRevision,
		package->flagCrates ? "true" : "false",
		package->flagCtrLetters ? "true" : "false",
		package->flagRelicCrates ? "true" : "false",
		package->flagAiNav ? "true" : "false",
		package->flagMinimap ? "true" : "false",
		package->flagGhosts ? "true" : "false",
		package->flagSpawns, package->flagCheckpoints);

	return wrote >= 0 && (size_t)wrote < dstSize;
}

static int Manager_ReadText(const char *path, char *dst, size_t dstSize, int *outMissing)
{
	FILE *file;
	size_t got;
	int extra;

	*outMissing = 0;
	file = fopen(path, "rb");
	if (file == NULL)
	{
		if (errno == ENOENT)
			*outMissing = 1;
		return 0;
	}
	got = fread(dst, 1, dstSize - 1, file);
	extra = fgetc(file);
	if (ferror(file) || extra != EOF)
	{
		fclose(file);
		return 0;
	}
	fclose(file);
	dst[got] = '\0';
	return 1;
}

static int Manager_WriteAtomic(const char *path, const char *text)
{
	char temp[CTR_CT_MANAGER_PATH_MAX];
	FILE *file;
	int wrote;
	int failed = 0;

	wrote = snprintf(temp, sizeof temp, "%s.tmp", path);
	if (wrote < 0 || (size_t)wrote >= sizeof temp)
		return 0;

	file = fopen(temp, "wb");
	if (file == NULL)
		return 0;
	if (fwrite(text, 1, strlen(text), file) != strlen(text))
		failed = 1;
	if (fflush(file) != 0)
		failed = 1;
	if (fclose(file) != 0)
		failed = 1;
	if (failed)
	{
		remove(temp);
		return 0;
	}

#if defined(_WIN32)
	if (!MoveFileExA(temp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
#else
	if (rename(temp, path) != 0)
#endif
	{
		remove(temp);
		return 0;
	}
	return 1;
}

const struct CustomTrackManagerPackage *CustomTrackManager_BabyTPark(void)
{
	return &s_babyTParkPackage;
}

const char *CustomTrackManager_StateText(int state)
{
	switch (state)
	{
	case CTR_CT_MANAGER_NOT_INSTALLED: return "Not Installed";
	case CTR_CT_MANAGER_VERIFYING: return "Verifying";
	case CTR_CT_MANAGER_MISSING_FILES: return "Missing Files";
	case CTR_CT_MANAGER_HASH_MISMATCH: return "Hash Mismatch";
	case CTR_CT_MANAGER_MANIFEST_MISSING: return "Verified - Finish Setup";
	case CTR_CT_MANAGER_MANIFEST_INVALID: return "Manifest Invalid";
	case CTR_CT_MANAGER_READY: return "Ready";
	case CTR_CT_MANAGER_UNSUPPORTED: return "Unsupported";
	case CTR_CT_MANAGER_INCOMPATIBLE: return "Incompatible Version";
	case CTR_CT_MANAGER_IO_ERROR: return "I/O Error";
	default: return "Unknown";
	}
}

int CustomTrackManager_PrepareFolder(const char *assetsRoot,
	                                  const struct CustomTrackManagerPackage *package,
	                                  struct CustomTrackManagerStatus *outStatus)
{
	char tracks[CTR_CT_MANAGER_PATH_MAX];
	char original[CTR_CT_MANAGER_PATH_MAX];
	char extensions[CTR_CT_MANAGER_PATH_MAX];
	static const char *extensionDirs[] = {"navigation", "placements", "ap-boxes", "cameras"};
	int i;

	if (outStatus == NULL)
		return 0;
	memset(outStatus, 0, sizeof *outStatus);
	if (!Manager_PackageSupported(package))
	{
		Manager_SetDetail(outStatus, CTR_CT_MANAGER_UNSUPPORTED, "Package registry entry is invalid or unsupported.");
		return 0;
	}
	if (!Manager_BuildPaths(assetsRoot, package, outStatus))
		return 0;

	if (!Manager_Join(tracks, sizeof tracks, assetsRoot, "tracks") ||
	    !Manager_Join(original, sizeof original, outStatus->packageRoot, "original") ||
	    !Manager_Join(extensions, sizeof extensions, outStatus->packageRoot, "extensions") ||
	    !Manager_CreateDirectory(assetsRoot) ||
	    !Manager_CreateDirectory(tracks) ||
	    !Manager_CreateDirectory(outStatus->packageRoot) ||
	    !Manager_CreateDirectory(original) ||
	    !Manager_CreateDirectory(extensions))
	{
		Manager_SetDetail(outStatus, CTR_CT_MANAGER_IO_ERROR, "Could not create the custom-track folder structure.");
		return 0;
	}

	for (i = 0; i < (int)(sizeof extensionDirs / sizeof extensionDirs[0]); i++)
	{
		char child[CTR_CT_MANAGER_PATH_MAX];
		if (!Manager_Join(child, sizeof child, extensions, extensionDirs[i]) || !Manager_CreateDirectory(child))
		{
			Manager_SetDetail(outStatus, CTR_CT_MANAGER_IO_ERROR, "Could not create extension folder '%s'.", extensionDirs[i]);
			return 0;
		}
	}

	return CustomTrackManager_ScanPackage(assetsRoot, package, outStatus) != CTR_CT_MANAGER_IO_ERROR;
}

int CustomTrackManager_ScanPackage(const char *assetsRoot,
	                                const struct CustomTrackManagerPackage *package,
	                                struct CustomTrackManagerStatus *outStatus)
{
	int levCandidates;
	int vrmCandidates;
	int levUnreadable;
	int vrmUnreadable;
	int manifestMissing;
	char original[CTR_CT_MANAGER_PATH_MAX];
	char foundLev[CTR_CT_MANAGER_PATH_MAX];
	char foundVrm[CTR_CT_MANAGER_PATH_MAX];
	char foundLevHash[NATIVE_SHA256_HEX_BYTES];
	char foundVrmHash[NATIVE_SHA256_HEX_BYTES];
	unsigned long foundLevBytes;
	unsigned long foundVrmBytes;
	char expectedManifest[CTR_CT_MANAGER_TEXT_MAX];
	char actualManifest[CTR_CT_MANAGER_TEXT_MAX];

	if (outStatus == NULL)
		return CTR_CT_MANAGER_IO_ERROR;
	memset(outStatus, 0, sizeof *outStatus);
	if (!Manager_PackageSupported(package))
	{
		Manager_SetDetail(outStatus, CTR_CT_MANAGER_UNSUPPORTED, "Package registry entry is invalid or unsupported.");
		return outStatus->state;
	}
	if (!Manager_BuildPaths(assetsRoot, package, outStatus))
		return outStatus->state;

	if (!Manager_Join(original, sizeof original, outStatus->packageRoot, "original") ||
	    !Manager_FindVerifiedSource(original, ".lev", package->levSha256,
	                                foundLev, foundLevHash, &foundLevBytes,
	                                &levCandidates, &levUnreadable) ||
	    !Manager_FindVerifiedSource(original, ".vrm", package->vrmSha256,
	                                foundVrm, foundVrmHash, &foundVrmBytes,
	                                &vrmCandidates, &vrmUnreadable))
	{
		Manager_SetDetail(outStatus, CTR_CT_MANAGER_IO_ERROR, "Could not scan the original track folder.");
		return outStatus->state;
	}
	if (levUnreadable || vrmUnreadable)
	{
		Manager_SetDetail(outStatus, CTR_CT_MANAGER_IO_ERROR, "One or more candidate track files could not be read.");
		return outStatus->state;
	}

	if (levCandidates == 0 && vrmCandidates == 0)
	{
		Manager_SetDetail(outStatus, CTR_CT_MANAGER_NOT_INSTALLED,
		                  "Download from Saphi, or add the LEV and VRM to original, then Rescan.");
		return outStatus->state;
	}
	if (levCandidates == 0 || vrmCandidates == 0)
	{
		Manager_SetDetail(outStatus, CTR_CT_MANAGER_MISSING_FILES,
		                  "No %s file was found in the original folder.", levCandidates == 0 ? "LEV" : "VRM");
		return outStatus->state;
	}
	if (foundLev[0] == '\0' || foundVrm[0] == '\0')
	{
		Manager_SetDetail(outStatus, CTR_CT_MANAGER_HASH_MISMATCH,
		                  "The LEV or VRM does not match %s %s from the official source.",
		                  package->title, package->version);
		return outStatus->state;
	}
	snprintf(outStatus->levPath, sizeof outStatus->levPath, "%s", foundLev);
	snprintf(outStatus->vrmPath, sizeof outStatus->vrmPath, "%s", foundVrm);
	snprintf(outStatus->actualLevSha256, sizeof outStatus->actualLevSha256, "%s", foundLevHash);
	snprintf(outStatus->actualVrmSha256, sizeof outStatus->actualVrmSha256, "%s", foundVrmHash);
	outStatus->levBytes = foundLevBytes;
	outStatus->vrmBytes = foundVrmBytes;

	if (!Manager_RenderManifest(package, outStatus, expectedManifest, sizeof expectedManifest))
	{
		Manager_SetDetail(outStatus, CTR_CT_MANAGER_UNSUPPORTED, "The package manifest is too large for manager-light.");
		return outStatus->state;
	}
	if (!Manager_ReadText(outStatus->manifestPath, actualManifest, sizeof actualManifest, &manifestMissing))
	{
		if (manifestMissing)
		{
			Manager_SetDetail(outStatus, CTR_CT_MANAGER_MANIFEST_MISSING,
			                  "Files verified. Finish Setup will create the local manifest.");
			return outStatus->state;
		}
		Manager_SetDetail(outStatus, CTR_CT_MANAGER_MANIFEST_INVALID, "manifest.json is unreadable or too large.");
		return outStatus->state;
	}
	if (strcmp(expectedManifest, actualManifest) != 0)
	{
		Manager_SetDetail(outStatus, CTR_CT_MANAGER_MANIFEST_INVALID,
		                  "manifest.json does not match the Alpha6 package registry; Verify can repair it.");
		return outStatus->state;
	}

	Manager_SetDetail(outStatus, CTR_CT_MANAGER_READY, "%s %s is verified and ready.", package->title, package->version);
	return outStatus->state;
}

int CustomTrackManager_FinalizePackage(const char *assetsRoot,
	                                    const struct CustomTrackManagerPackage *package,
	                                    struct CustomTrackManagerStatus *outStatus)
{
	char manifest[CTR_CT_MANAGER_TEXT_MAX];
	int state = CustomTrackManager_ScanPackage(assetsRoot, package, outStatus);

	if (state == CTR_CT_MANAGER_READY)
		return 1;
	if (state != CTR_CT_MANAGER_MANIFEST_MISSING && state != CTR_CT_MANAGER_MANIFEST_INVALID)
		return 0;
	if (!Manager_RenderManifest(package, outStatus, manifest, sizeof manifest) ||
	    !Manager_WriteAtomic(outStatus->manifestPath, manifest))
	{
		Manager_SetDetail(outStatus, CTR_CT_MANAGER_IO_ERROR, "Could not write manifest.json.");
		return 0;
	}
	return CustomTrackManager_ScanPackage(assetsRoot, package, outStatus) == CTR_CT_MANAGER_READY;
}

static int Manager_TextEqual(const char *left, const char *right)
{
	return left != NULL && right != NULL && strcmp(left, right) == 0;
}

static int Manager_RequirementMatches(const struct CustomTrackManagerPackage *package,
	                                  const struct CustomTrackManagerRequirement *requirement)
{
	return requirement != NULL &&
	       Manager_TextEqual(requirement->id, package->id) &&
	       Manager_TextEqual(requirement->packageUuid, package->packageUuid) &&
	       Manager_TextEqual(requirement->packageVersion, package->version) &&
	       Manager_TextEqual(requirement->minimumClientVersion, package->minimumClientVersion) &&
	       Manager_TextEqual(requirement->minimumApworldVersion, package->minimumApworldVersion) &&
	       NativeSha256_HexEquals(requirement->levSha256, package->levSha256) &&
	       NativeSha256_HexEquals(requirement->vrmSha256, package->vrmSha256) &&
	       Manager_TextEqual(requirement->navigationUuid, package->navigationUuid) &&
	       requirement->navigationRevision == package->navigationRevision &&
	       requirement->laps == package->laps &&
	       requirement->boxes == 0 &&
	       requirement->flagCrates == package->flagCrates &&
	       requirement->flagCtrLetters == package->flagCtrLetters &&
	       requirement->flagRelicCrates == package->flagRelicCrates &&
	       requirement->flagAiNav == package->flagAiNav &&
	       requirement->flagMinimap == package->flagMinimap &&
	       requirement->flagGhosts == package->flagGhosts &&
	       requirement->flagSpawns == package->flagSpawns &&
	       requirement->flagCheckpoints == package->flagCheckpoints;
}

int CustomTrackManager_Preflight(const char *assetsRoot,
	                              const struct CustomTrackManagerRequirement *requirement,
	                              int autoFinalize,
	                              struct CustomTrackManagerStatus *outStatus)
{
	const struct CustomTrackManagerPackage *package = CustomTrackManager_BabyTPark();
	int state;

	if (outStatus == NULL)
		return CTR_CT_MANAGER_IO_ERROR;
	memset(outStatus, 0, sizeof *outStatus);

	if (requirement == NULL || !Manager_TextEqual(requirement->id, package->id))
	{
		Manager_SetDetail(outStatus, CTR_CT_MANAGER_UNSUPPORTED,
		                  "This Alpha6 client does not recognize the seed's custom track package.");
		return outStatus->state;
	}
	if (!Manager_RequirementMatches(package, requirement))
	{
		Manager_SetDetail(outStatus, CTR_CT_MANAGER_INCOMPATIBLE,
		                  "The seed's Baby T Park identity does not match the Alpha6 package registry.");
		return outStatus->state;
	}

	state = CustomTrackManager_ScanPackage(assetsRoot, package, outStatus);
	if (autoFinalize && (state == CTR_CT_MANAGER_MANIFEST_MISSING ||
	                     state == CTR_CT_MANAGER_MANIFEST_INVALID))
	{
		CustomTrackManager_FinalizePackage(assetsRoot, package, outStatus);
		state = outStatus->state;
	}
	return state;
}

int CustomTrackManager_RenderYaml(const struct CustomTrackManagerPackage *package,
	                               const struct CustomTrackManagerStatus *status,
	                               char *dst, size_t dstSize)
{
	int wrote;

	if (!Manager_PackageSupported(package) || status == NULL || status->state != CTR_CT_MANAGER_READY ||
	    dst == NULL || dstSize == 0)
		return 0;

	wrote = snprintf(dst, dstSize,
		"custom_tracks:\n"
		"  %s:\n"
		"    package_uuid: %s\n"
		"    package_version: %s\n"
		"    minimum_client_version: %s\n"
		"    minimum_apworld_version: %s\n"
		"    lev_sha256: %s\n"
		"    vrm_sha256: %s\n"
		"    navigation:\n"
		"      uuid: %s\n"
		"      revision: %u\n"
		"    laps: %d\n"
		"    replaces: purple_gem_cup\n"
		"    boxes: false\n"
		"    flags:\n"
		"      crates: %s\n"
		"      ctr_letters: %s\n"
		"      relic_crates: %s\n"
		"      ai_nav: %s\n"
		"      minimap: %s\n"
		"      ghosts: %s\n"
		"      spawns: %d\n"
		"      checkpoints: %d\n",
		package->id, package->packageUuid, package->version,
		package->minimumClientVersion, package->minimumApworldVersion,
		package->levSha256, package->vrmSha256,
		package->navigationUuid, package->navigationRevision, package->laps,
		package->flagCrates ? "true" : "false",
		package->flagCtrLetters ? "true" : "false",
		package->flagRelicCrates ? "true" : "false",
		package->flagAiNav ? "true" : "false",
		package->flagMinimap ? "true" : "false",
		package->flagGhosts ? "true" : "false",
		package->flagSpawns, package->flagCheckpoints);

	return wrote >= 0 && (size_t)wrote < dstSize;
}

int CustomTrackManager_SaveYaml(const char *assetsRoot,
	                             const struct CustomTrackManagerPackage *package,
	                             const struct CustomTrackManagerStatus *status,
	                             struct CustomTrackManagerStatus *outStatus)
{
	char yaml[CTR_CT_MANAGER_TEXT_MAX];

	if (status == NULL || outStatus == NULL)
		return 0;
	if (status != outStatus)
		memcpy(outStatus, status, sizeof *outStatus);
	if (!CustomTrackManager_RenderYaml(package, status, yaml, sizeof yaml))
	{
		Manager_SetDetail(outStatus, CTR_CT_MANAGER_IO_ERROR, "YAML export requires a Ready package.");
		return 0;
	}
	if (!Manager_BuildPaths(assetsRoot, package, outStatus) || !Manager_WriteAtomic(outStatus->yamlPath, yaml))
	{
		Manager_SetDetail(outStatus, CTR_CT_MANAGER_IO_ERROR, "Could not write custom_tracks.generated.yaml.");
		return 0;
	}
	Manager_SetDetail(outStatus, CTR_CT_MANAGER_READY, "Saved the verified custom_tracks YAML fragment.");
	return 1;
}

#endif // CTR_CUSTOM_TRACKS
