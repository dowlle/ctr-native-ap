#ifndef NATIVE_XDELTA_DECODE_H
#define NATIVE_XDELTA_DECODE_H
/* Streaming VCDIFF (RFC 3284, xdelta3) decode for the track importer.
   Decode only: the vendored xdelta3 3.1 (branch release3_1_apl, Apache 2.0)
   is built without its encoder, without LZMA and without FGK. The source is
   read through a callback in fixed blocks, so the whole base disc is never in
   memory; the target is handed to a sink window by window, in target order.
   Every window must carry an adler32, and it is checked before the window
   reaches the sink; a window without one is refused. */
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

enum NativeXdeltaResult
{
    NATIVE_XDELTA_OK = 1,
    NATIVE_XDELTA_FAILED = 0,          /* Read error or malformed patch. */
    NATIVE_XDELTA_NOT_VCDIFF = -1,     /* No VCDIFF magic. */
    NATIVE_XDELTA_UNSUPPORTED = -2,    /* Secondary compression other than DJW (LZMA, FGK). */
    NATIVE_XDELTA_CHECKSUM = -3,       /* A target window's adler32 does not match. */
    NATIVE_XDELTA_SOURCE_RANGE = -4,   /* A window reads past the end of the source. */
    NATIVE_XDELTA_STOPPED = -5,        /* The sink asked to stop. */
    NATIVE_XDELTA_CANCELLED = -6,
    NATIVE_XDELTA_NO_CHECKSUM = -7     /* A window carries no adler32. */
};

/* Patch reader: copy up to size bytes, return the count, 0 at end, -1 on error. */
typedef long (*NativeXdeltaReadPatch)(void *context, void *destination, size_t size);
/* Source reader: read exactly size bytes at offset (size may be short only at
   the end of the source). Return the count, or -1 on error. */
typedef long (*NativeXdeltaReadSource)(void *context, unsigned long long offset, void *destination, size_t size);
/* Target sink: return 1 to continue, 0 to stop the decode. */
typedef int (*NativeXdeltaWriteTarget)(void *context, const void *bytes, size_t size);

struct NativeXdeltaIo
{
    NativeXdeltaReadPatch readPatch;
    NativeXdeltaReadSource readSource;
    NativeXdeltaWriteTarget writeTarget;
    void *context;
    unsigned long long sourceBytes;
    const volatile int *cancel; /* Optional; polled between steps. */
};

/* Inspect the first bytes of a patch. Returns NATIVE_XDELTA_OK when the decode
   path supports it, NOT_VCDIFF or UNSUPPORTED otherwise. */
int NativeXdelta_CheckHeader(const void *bytes, size_t size);
/* Decode the whole patch. Returns a NativeXdeltaResult; error gets one line. */
int NativeXdelta_Decode(const struct NativeXdeltaIo *io, char *error, size_t errorSize);

#ifdef __cplusplus
}
#endif
#endif
