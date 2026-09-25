#ifndef NATIVE_CUSTOM_PACKAGE_H
#define NATIVE_CUSTOM_PACKAGE_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

#define CTR_PACKAGE_FILE_MAX 32
/* Unicode 15.0.0, NFC, 1..160 codepoints, no category-C or edge whitespace. */
int CustomPackage_ValidateTitle(const char *utf8, size_t size);
struct CustomPackageFile
{
    char role[24];
    char path[241];
    char sha256[65];
    unsigned int bytes;
};
struct CustomPackageManifest
{
    char sha256[65];
    char uuid[37];
    char version[65];
    char title[641]; /* Up to 160 Unicode codepoints, four UTF-8 bytes each. */
    unsigned int count;
    struct CustomPackageFile files[CTR_PACKAGE_FILE_MAX];
};

/* Parse/normalize package metadata and compare the COMPLETE manifest pin.
   No catalogue membership or runtime certification is inferred. */
int CustomPackage_ParseManifest(const char *json, size_t size, const char *expectedSha256,
                                struct CustomPackageManifest *out, char *error, size_t errorSize);

struct CustomPackageOwned;
struct CustomContentVerification;
struct CustomPackageInput
{
    const char *role;
    const void *data;
    size_t size;
};

/* Copy the complete declared revision, hash the owned copies, then run CTR's
   structural LEV/VRM verifier on those same bytes. Input order is immaterial.
   The caller owns input memory and must keep it stable for this synchronous
   call. On success no input pointers are retained. *out must initially be NULL;
   failure never replaces an existing package. This grants no mode certification
   and does not validate supplemental file encodings or filesystem provenance. */
int CustomPackage_AcquireBuffers(const char *json, size_t size, const char *expectedSha256,
    const struct CustomPackageInput *inputs, size_t count, struct CustomPackageOwned **out,
    char *error, size_t errorSize);
/* Read manifest.json and its declared files from an absolute package directory.
   Refuse symlink/reparse components and non-regular files. A successful result
   owns the snapshot; no source path is retained for later gameplay reads.
   Windows currently requires a drive-qualified path, not UNC/device syntax. */
int CustomPackage_AcquireDirectory(const char *directory, const char *expectedSha256,
    struct CustomPackageOwned **out, char *error, size_t errorSize);
/* Retain an immutable snapshot without reopening files or copying asset bytes.
   Caller must hold a live reference throughout this call; *out must be NULL.
   Each retained handle must be freed exactly once. Different handles may be
   released concurrently, but access to the same handle requires synchronization. */
int CustomPackage_Retain(struct CustomPackageOwned *package, struct CustomPackageOwned **out);
/* Bytes owned by this snapshot: descriptor/report plus all file buffers.
   Excludes allocator overhead and caller/engine allocations; NULL returns zero. */
size_t CustomPackage_SnapshotBytes(const struct CustomPackageOwned *package);
/* Read geometry-bound authored standalone laps from pinned race_settings.
   Returns zero laps on refusal; no retail/catalogue/default substitution. */
int CustomPackage_GetRaceLaps(const struct CustomPackageOwned *package, unsigned int *laps,
    char *error, size_t errorSize);
void CustomPackage_Free(struct CustomPackageOwned **package);
int CustomPackage_GetManifest(const struct CustomPackageOwned *package, struct CustomPackageManifest *out);
int CustomPackage_GetPairReport(const struct CustomPackageOwned *package, struct CustomContentVerification *out);
struct CustomPackageRelicTargets
{
    unsigned int laps;
    int ticks[3]; /* Sapphire, gold, platinum; exactly 960 ticks per second. */
};
/* Interpret the pinned relic_targets sidecar from owned bytes. Bind its LEV
   and VRM hashes to this revision; never fall back to a retail target table.
   Schema validity is not author authentication or runtime certification.
   On refusal out is zeroed; missing/unknown sidecars fail explicitly. */
int CustomPackage_GetRelicTargets(const struct CustomPackageOwned *package,
    struct CustomPackageRelicTargets *out, char *error, size_t errorSize);
/* A bounded copy, never a borrowed mutable pointer. On refusal the destination
   is unchanged. Optional written reports the exact file size on success only. */
int CustomPackage_CopyFile(const struct CustomPackageOwned *package, const char *role,
                          void *destination, size_t capacity, size_t *written);
int CustomPackage_CopyManifestJSON(const struct CustomPackageOwned *package, void *destination,
                                  size_t capacity, size_t *written);
/* Install only an already acquired revision into an existing absolute store.
   Returns 1 for a newly published revision, 2 for a reverified existing revision,
   0 for failure. Never overwrites a revision. Failed .pending-* directories are
   retained, never treated as installed, and can be reviewed/cleaned separately. */
int CustomPackage_Install(const struct CustomPackageOwned *package, const char *store,
                          char *error, size_t errorSize);
/* Publish an already verified snapshot in the managed assets store, creating
   only its tracks/packages parents with the same directory safety checks. */
int CustomPackage_InstallAssets(const struct CustomPackageOwned *package, const char *assets,
                                char *error, size_t errorSize);
/* Explicit local import: ASSETS/tracks/inbox/PIN -> ASSETS/tracks/packages/PIN.
   No download or update inference. Source is verified before store creation. */
int CustomPackage_InstallInbox(const char *assets, const char *pin, char *error, size_t errorSize);
/* Main-thread job API. One install at a time; no game/UI pointers enter worker.
   Poll returns -1 idle, 0 running, 1 completed; result uses Install's 0/1/2. */
int CustomPackage_StartInboxInstall(const char *assets, const char *pin);
int CustomPackage_PollInboxInstall(int *result, char pin[65], char *error, size_t errorSize);

/* Verified local metadata for UI grouping/admission hints, never Ready evidence. */
struct CustomPackageLocalInfo
{
    char uuid[37], author[81];
    int trackID, levID, vrmID;
    int arcade; /* Owned structural prerequisites and supported lap storage. */
};
struct CustomPackageStoreEntry
{
    char pin[65];
    int verified;
    struct CustomPackageManifest manifest; /* Zero on refusal, not guessed from directory name. */
    char error[257];
    struct CustomPackageLocalInfo local;
};
typedef void (*CustomPackageStoreVisitor)(const struct CustomPackageStoreEntry *entry, void *context);
/* Sorted, bounded read-only discovery of exact lowercase hash names. Staging
   and unrelated names are ignored. Each revision is fully re-acquired; damaged
   entries remain visible with a reason. Copy callback data before returning.
   Discard accumulated rows if the overall scan fails. No mode certification. */
int CustomPackage_ScanStore(const char *store, CustomPackageStoreVisitor visitor, void *context,
                           char *error, size_t errorSize);
struct CustomTrackLibrary;
int CustomPackage_StartStoreScan(const char *store);
/* -1 idle, 0 running, 1 applied, -2 failed (old library retained). */
int CustomPackage_PollStoreScan(struct CustomTrackLibrary *library, char *error, size_t errorSize);

#ifdef __cplusplus
}
#endif
#endif
