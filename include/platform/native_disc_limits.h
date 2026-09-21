#ifndef NATIVE_DISC_LIMITS_H
#define NATIVE_DISC_LIMITS_H

// Shared size contract for a persisted external disc-image path (issue #334,
// implementation slice 2). The in-memory buffer holds the path plus its NUL,
// and the content persisted in disc-path.txt is bounded one byte below the
// buffer so a saved path is either stored verbatim or rejected, never
// truncated. Kept in its own tiny header so the path store, the resolution
// state machine and the startup sequence agree without depending on each other.
#define NATIVE_DISC_PATH_MAX         1024
#define NATIVE_DISC_PATH_CONTENT_MAX (NATIVE_DISC_PATH_MAX - 1)

#endif // NATIVE_DISC_LIMITS_H
