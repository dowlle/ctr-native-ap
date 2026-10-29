#ifndef AP_UPDATE_CORE_H
#define AP_UPDATE_CORE_H

// Signed in-game updater, trust core (issue #301).
//
// Everything here works on bytes the caller already holds: no network, no
// threads, no game state. The network worker (ap_update_net.cpp) feeds it the
// downloaded manifest, signature and archive, and the host harness
// tools/test-update-core.cpp feeds it temporary signed releases. Keeping the
// trust decisions in one place is the point: the code that decides whether an
// update may be installed is the code the harness runs.
//
// Trust order, enforced by VerifyRelease:
//   1. the minisign signature over the exact manifest bytes verifies against
//      the public key compiled into this executable (key id and both the file
//      signature and the trusted-comment signature);
//   2. only then is the manifest parsed: exactly {version, artifacts}, the two
//      client archive names for that version, lowercase SHA-256 values;
//   3. the signed version equals the release tag that was offered, and is a
//      plain vX.Y.Z strictly newer than the running build (no downgrade, no
//      replay of an older signed release, no prerelease);
//   4. the platform archive's SHA-256 equals the signed value.
// The archive is then validated member by member, and the install set is
// taken out of it: the client executable, ctr.apworld, the files under
// assets/tracks/cortex-vortex/ (the custom track the release ships, whose
// hashes the client checks at load, so it must move with the executable) and
// the whole solo-runtime/ folder (#89: the bundled Python runtime, replaced as
// one folder so no file of an older runtime stays behind). Nothing else in
// the archive is installed: config.ini, ap-config.txt, the rest of assets/,
// memory cards, AP state and the player's solo/ folder are never touched.
//
// The signed manifest covers the two archives, not single files. Each
// installed file is therefore trusted through the archive hash: its SHA-256
// is taken from the verified archive bytes at extraction, and every staged
// file is read back and checked against that value before it is written into
// place, and again just before the swap.
//
// The install step (Stage / Apply / StartupCheck / ConfirmStarted) goes through
// an injectable file-operation table so the harness can fail any step and check
// that every installed file survives. All files move as one unit: any failure
// puts every file already moved back.
//
// solo-runtime/ is staged as the folder solo-runtime.update/ and swapped with
// two folder renames (solo-runtime -> solo-runtime.old, then the staged folder
// in). Its RUNTIME.txt names every runtime file's hash and the apworld hash,
// so the SHA-256 of RUNTIME.txt identifies which runtime a folder holds during
// recovery. The caller must not apply while a solo session or job runs: the
// runtime's interpreter would be running from the folder being replaced.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The release public key (ctr-release-minisign.pub, key id 46F129D8BEF2D633).
// Public by design; the secret key never exists outside the maintainer's
// offline storage. Changing this line is a reviewed key rotation.
#define AP_UPDATE_PUBLIC_KEY "RWQz1vK+2CnxRlzUFiAM8FlyI706TxjELuc+8d4N9uh06nS1zAKGuD6x"

namespace ap_update
{

typedef std::vector<unsigned char> Bytes;

enum Platform
{
	PLATFORM_WINDOWS = 0,
	PLATFORM_LINUX = 1
};

enum Result
{
	OK = 0,
	ERR_KEY,              // the compiled-in public key does not parse
	ERR_SIG_FORMAT,       // the .minisig file is not a minisign signature
	ERR_SIG_KEY_ID,       // signed by another key
	ERR_SIGNATURE,        // file signature does not verify
	ERR_COMMENT_SIGNATURE,// trusted comment signature does not verify
	ERR_MANIFEST,         // signed, but not the manifest shape we accept
	ERR_TAG_MISMATCH,     // signed version is not the release that was offered
	ERR_NOT_NEWER,        // signed version is not newer than this build
	ERR_HASH,             // archive bytes do not match the signed SHA-256
	ERR_ARCHIVE,          // archive member validation failed
	ERR_SIZE,             // a size limit was exceeded
	ERR_IO,               // a file operation failed
	ERR_STAGED_CHANGED,   // the staged executable no longer matches
	ERR_UNDO_FAILED       // a swap step failed and the undo failed too: .old copies hold the previous files
};

const char *ResultText(Result r);

// Hard limits. The executable is about 12 MB; the largest runtime file (the
// Linux libpython) about 39 MB; the Linux runtime unpacks to about 70 MB.
const size_t kMaxReleaseJson = 1024 * 1024;
const size_t kMaxManifest = 4096;
const size_t kMaxSignature = 4096;
// A 0.3.0 archive with the solo runtime: Windows 14 MB, Linux 22 MB.
const size_t kMaxArchive = 64u * 1024u * 1024u;
const size_t kMaxExecutable = 64u * 1024u * 1024u;
const size_t kMaxUnpacked = 256u * 1024u * 1024u;
const size_t kMaxMembers = 4096;

// ── Small primitives (exposed for the harness) ──
bool Base64Decode(const std::string &text, Bytes &out);
std::string Sha256Hex(const unsigned char *data, size_t size);
inline std::string Sha256Hex(const Bytes &b) { return Sha256Hex(b.data(), b.size()); }

// A strict stable tag: "v" + three dot-separated decimal numbers, nothing else.
bool IsStableTag(const std::string &tag);

// The two client archive names a manifest for `tag` must list.
std::string ArchiveName(const std::string &tag, Platform platform);
// The executable inside the client archive.
const char *ExecutableName(Platform platform);

// ── Signature ──
struct PublicKey
{
	unsigned char keyId[8];
	unsigned char key[32];
};
bool ParsePublicKey(const std::string &base64, PublicKey &out);

// Verify a minisign signature file over `message`. Accepts both minisign
// algorithms ("Ed" over the message, "ED" over its BLAKE2b-512, the default
// since minisign 0.10), always with the trusted-comment signature checked.
Result VerifyMinisign(const PublicKey &key, const Bytes &message, const std::string &sigFile,
                      std::string *trustedComment);

// ── Manifest ──
struct Manifest
{
	std::string version;
	std::string windowsSha256;
	std::string linuxSha256;
};
// Strict reader: valid UTF-8 JSON object with exactly "version" and
// "artifacts", no duplicate keys, the two archive entries for that version
// and nothing else, lowercase 64-hex digests. Call only on signed bytes.
Result ParseManifest(const Bytes &raw, Manifest &out);

// ── Release check (the offer) ──
struct ReleaseInfo
{
	std::string tag;
};
// Reads the GitHub releases/latest JSON. Offer-visible only when: not a draft,
// not a prerelease, a stable tag, and the asset list names manifest.json,
// manifest.json.minisig and BOTH client archives for that tag. Returns false
// otherwise (a silent "no update").
bool ParseLatestRelease(const Bytes &json, ReleaseInfo &out);
// 1 when `tag` should be offered to a build whose compatibility version is
// `running` and which declined `declined` (may be empty).
bool ShouldOffer(const std::string &tag, const std::string &running, const std::string &declined);

// ── Full verification of a downloaded release ──
Result VerifyRelease(const std::string &publicKeyBase64, const Bytes &manifest,
                     const std::string &signature, const std::string &offeredTag,
                     const std::string &runningVersion, Platform platform, const Bytes &archive);

// ── Archive ──
// Members must all live under "ctr-archipelago-<tag>/", be regular files or
// directories, have no "..", absolute, drive or backslash paths, no
// duplicates (also by case), and stay inside the size limits. ZIP: stored or
// deflated, no encryption, CRC-32 checked. tar.gz: ustar with pax path
// records; links, devices and sparse files are refused.
struct InstallFile
{
	std::string rel;    // path inside the release folder, '/' separated
	Bytes data;
	std::string sha256; // of `data`, taken from the verified archive
	bool executable = false; // the executable, or a tar member with an x bit
};
// The bundled solo runtime folder (#89) and its identity file.
extern const char kRuntimeDir[];   // "solo-runtime"
extern const char kRuntimeIdRel[]; // "solo-runtime/RUNTIME.txt"
// 1 when a release-relative path is part of the install set.
bool IsInstallPath(const std::string &rel, Platform platform);
// 1 for a file inside solo-runtime/ (any depth).
bool IsRuntimePath(const std::string &rel);
// Validate every member and return the install set, executable first. The
// executable must be present exactly once; a set with runtime files must
// carry solo-runtime/RUNTIME.txt.
Result ExtractInstallSet(const Bytes &archive, Platform platform, const std::string &tag,
                         std::vector<InstallFile> &out);
// The executable alone (harness convenience).
Result ExtractExecutable(const Bytes &archive, Platform platform, const std::string &tag,
                         Bytes &exeOut);

// ── Where the updater may connect ──
// The check reads https://api.github.com/repos/dowlle/ctr-native-ap/releases/latest.
// Downloads start at https://github.com/dowlle/ctr-native-ap/releases/download/<tag>/<name>,
// built here from a stable tag and a fixed asset name, never from a URL in the
// API response. GitHub answers those with a redirect to its release-asset
// storage; a redirect is followed only over HTTPS, at most four times, and only
// to github.com release paths of this repository or the two GitHub asset hosts.
// Integrity never rests on this list: the signature and hashes do that. It
// keeps the client from talking to anything outside the release topology.
std::string LatestReleaseUrl();
std::string AssetUrl(const std::string &tag, const std::string &name);
const int kMaxRedirects = 4;
// Split an https URL into host and path when it is allowed. `redirect` is
// true for Location targets (the asset hosts are only reachable that way).
bool AllowedUrl(const std::string &url, bool redirect, std::string &host, std::string &path);

// ── HTTP/1.1 response framing (the Linux transport reads raw bytes) ──
enum HttpParse
{
	HTTP_COMPLETE = 0,
	HTTP_INCOMPLETE,   // connection closed early: fewer bytes than Content-Length, or an unfinished chunked body
	HTTP_MALFORMED,
	HTTP_TOO_LARGE
};
struct HttpResponse
{
	int status;
	std::string location;
	long long contentLength; // -1 when absent
	Bytes body;
};
// Parse a complete response read until the server closed the connection.
// Only a body that provably ended (Content-Length reached exactly, or the
// final zero chunk) counts as complete.
HttpParse ParseHttpResponse(const Bytes &raw, size_t maxBody, HttpResponse &out);
// Content-Length announced in a header block, or -1; used for progress.
long long HeaderContentLength(const std::string &headers);

// ── Install ──
struct FsOps
{
	void *ctx;
	bool (*writeFile)(void *ctx, const std::string &path, const Bytes &data);
	bool (*readFile)(void *ctx, const std::string &path, size_t maxBytes, Bytes &out);
	bool (*renameFile)(void *ctx, const std::string &from, const std::string &to); // never replaces
	bool (*removeFile)(void *ctx, const std::string &path); // true when gone afterwards
	bool (*exists)(void *ctx, const std::string &path);
	bool (*makeExecutable)(void *ctx, const std::string &path);
	bool (*makeDir)(void *ctx, const std::string &path);    // true when it exists afterwards
	bool (*isLink)(void *ctx, const std::string &path);     // a symlink or junction (not every reparse point)
	// Optional (may be null, Windows has neither): a hard link that never
	// replaces, and a rename that atomically replaces an existing target. With
	// both, the executable swap leaves no moment without an executable.
	bool (*linkFile)(void *ctx, const std::string &existing, const std::string &newLink);
	bool (*replaceFile)(void *ctx, const std::string &from, const std::string &to);
	// Remove a folder and everything in it. A link or junction inside is
	// removed itself, never followed. True when nothing is left at `path`.
	bool (*removeTree)(void *ctx, const std::string &path);
};
// Real file operations (UTF-8 paths; wide APIs on Windows).
FsOps RealFsOps();

// Per installed file F: F.update.part while writing, F.update when staged,
// F.old while the previous version is kept. The runtime folder R: R.update
// (its files written inside, each through a .part) and R.old.
std::string StagedPath(const std::string &file);
std::string PartPath(const std::string &file);
std::string OldPath(const std::string &file);
// Beside the executable: the marker after a swap (tag, then the release paths
// that have a .old copy) and the staging journal (release paths being staged).
std::string MarkerPath(const std::string &exe);
std::string JournalPath(const std::string &exe);
// The swap journal: written and flushed before the first rename of Apply,
// removed once the marker is written. A journal without a marker at launch
// means a swap was cut short and StartupCheck puts the .old copies back.
std::string SwapJournalPath(const std::string &exe);
// Where the assets folder really is (the game may use the parent folder's
// `assets` or a case variant such as `Assets`). Release paths starting with
// "assets/" install under it. Empty (the default) means <exe folder>/assets.
void SetAssetDir(const std::string &dir);
// Install directory of `exe` joined with a release path.
std::string TargetPath(const std::string &exe, const std::string &rel);

struct StagedFile
{
	std::string rel;
	std::string sha256;
};

// Write every file of the set beside its installed copy as F.update, each
// through a .part file that is read back and hashed against the archive
// value. Missing folders inside the install set are created; a folder on the
// way that is a link or junction is refused, so a shared folder is never
// written. Any failure removes everything staged so far.
Result Stage(const FsOps &fs, const std::string &exe, const std::vector<InstallFile> &files,
             std::vector<StagedFile> &staged);

// Remove a staged set that will not be installed (cancel, dismiss).
void DiscardStaged(const FsOps &fs, const std::string &exe, const std::vector<StagedFile> &staged);

// Swap the staged set in as one unit. Re-hashes every staged file first,
// removes an older marker (failing if it cannot), writes the swap journal,
// then for each file: the installed copy (if any) is renamed to F.old
// (replacing an older rollback copy), then F.update is renamed into place.
// The runtime folder follows the files the same way. The executable goes
// last; where atomic replace exists (Linux) the running file is kept as .old
// by a hard link, or by a copy where the file system has no hard links, and
// the new one renamed over it, so the executable path never vanishes. Any failed
// step undoes every step already taken, newest first, so the previous files
// are all back (ERR_IO). When the undo itself fails the journal stays and the
// result is ERR_UNDO_FAILED. Writes the marker on success.
Result Apply(const FsOps &fs, const std::string &exe, const std::vector<StagedFile> &staged,
             const std::string &newTag);

enum StartupState
{
	STARTUP_CLEAN = 0,       // nothing to do
	STARTUP_UPDATED = 1,     // this build was just installed; .old copies wait for ConfirmStarted
	STARTUP_ROLLED_BACK = 2, // a marker names another version: the player rolled back by hand
	STARTUP_RECOVERED = 3,   // a cut-short swap was found and the previous files were put back
	STARTUP_RESTORE_FAILED = 4 // ... and some file could not be put back; .old copies remain
};
// At launch: finish or undo a cut-short swap from the swap journal, delete any
// staged or partial file from an earlier session (an unapplied download is
// never trusted across launches), then read the marker.
StartupState StartupCheck(const FsOps &fs, const std::string &exe, const std::string &runningTag);
// The new build reached the main menu: delete the .old copies and the marker.
void ConfirmStarted(const FsOps &fs, const std::string &exe);

// ── One staging worker at a time ──
// The network worker keeps one of these under its own mutex. Staging writes
// shared paths (F.update, the journal), so a second worker must never start
// while one stages, and a cancel must not hand the paths to a new download.
struct WorkSession
{
	unsigned generation = 0;
	bool staging = false;
};
// A worker of `gen` claims staging. False when it was cancelled or superseded
// (generation changed) or another worker is staging.
bool ClaimStaging(WorkSession &s, unsigned gen);
void ReleaseStaging(WorkSession &s);
// Cancel and a new download are refused while a worker stages.
bool CancelAllowed(const WorkSession &s);
bool DownloadAllowed(const WorkSession &s);

} // namespace ap_update

#endif
