#ifndef NATIVE_DISC_PATH_STORE_H
#define NATIVE_DISC_PATH_STORE_H

#include <stddef.h>

// The remembered external disc image path (issue #334, implementation slice 2).
//
// The path lives in its own one-line file, "disc-path.txt", next to config.ini
// in the working directory the game runs from. It is deliberately NOT a
// config.ini key: config.ini is a user-edited options file with its own
// preserve-everything save, and threading a path through it made every ordinary
// settings save depend on the disc feature's parsing rules. A separate file
// keeps the two apart: a damaged or missing disc-path.txt costs one extra trip
// through the first-run picker and nothing else.
//
// File format: the UTF-8 path, then a single "\n". A UTF-8 BOM and CRLF line
// endings are tolerated on load (a player may have edited the file in a Windows
// text editor). Nothing else is stored, so there is no format to version.
//
// Both operations are binary-mode and use the UTF-8 filesystem helpers
// (platform/native_fs_utf8.h), so a non-ASCII path works on Windows and no text
// translation can make the bytes on disk differ from the bytes written.

// Name of the store file, relative to the working directory.
#define NATIVE_DISC_PATH_STORE_FILE "disc-path.txt"

// Read the remembered path into out (at most outSize bytes including the
// terminator). Returns 1 and a validated, never-truncated path, or 0 for "no
// remembered path": the file is missing (silent), or its content is empty,
// longer than the path limit, carries an embedded NUL or fails
// NativeDiscPath_Validate (one stderr status line each). On 0, out is set to
// the empty string when outSize allows.
int NativeDiscPathStore_Load(char *out, size_t outSize);

// Remember path. Writes an exclusively created temporary sibling, flushes it to
// stable storage and renames it over disc-path.txt, so a crash mid-save leaves
// either the old file or the new one. Returns 1 on success, 0 on failure after
// reporting one stderr line. A failure is a warning only: nothing else depends
// on the file, and the next launch simply asks for the disc again.
int NativeDiscPathStore_Save(const char *path);

#endif // NATIVE_DISC_PATH_STORE_H
