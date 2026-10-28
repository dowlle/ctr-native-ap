/* Decode-only build of the vendored xdelta3 (vendor/xdelta3, Apache 2.0) and
   the small streaming wrapper the track importer uses. See
   include/platform/native_xdelta_decode.h. */
/* xdelta3.h sets its own feature macros, so it comes before any system
   header; static_assert is the C11 keyword it expects. */
#ifndef static_assert
#define static_assert _Static_assert
#endif
#define XD3_ENCODER 0
#define XD3_MAIN 0
#define SECONDARY_DJW 1
#define SECONDARY_FGK 0
#define SECONDARY_LZMA 0
#define SIZEOF_UNSIGNED_LONG_LONG 8
#ifndef SIZEOF_SIZE_T
#if defined(__SIZEOF_SIZE_T__)
#define SIZEOF_SIZE_T __SIZEOF_SIZE_T__
#elif defined(_WIN64)
#define SIZEOF_SIZE_T 8
#else
#define SIZEOF_SIZE_T 4
#endif
#endif
#include "../vendor/xdelta3/xdelta3.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <platform/native_xdelta_decode.h>

#define XDELTA_BLOCK_SHIFT 20u                 /* 1 MiB source blocks. */
#define XDELTA_BLOCKS 8                        /* 8 MiB source cache. */
#define XDELTA_INPUT_CHUNK (64u * 1024u)

struct XdeltaBlock
{
    unsigned char *bytes;
    xoff_t number;
    usize_t size;
    unsigned long long used;
};

static void xdelta_error(char *error, size_t errorSize, const char *text)
{
    if (error && errorSize) snprintf(error, errorSize, "%s", text);
}

int NativeXdelta_CheckHeader(const void *bytes, size_t size)
{
    const unsigned char *p = (const unsigned char *)bytes;
    if (!p || size < 5 || p[0] != 0xd6 || p[1] != 0xc3 || p[2] != 0xc4 || p[3] != 0x00)
        return NATIVE_XDELTA_NOT_VCDIFF;
    if (p[4] & VCD_SECONDARY)
    {
        if (size < 6) return NATIVE_XDELTA_NOT_VCDIFF;
        if (p[5] != VCD_DJW_ID) return NATIVE_XDELTA_UNSUPPORTED;
    }
    return NATIVE_XDELTA_OK;
}

static int xdelta_get_block(const struct NativeXdeltaIo *io, xd3_source *source,
                            struct XdeltaBlock *blocks, unsigned long long *clock)
{
    const xoff_t number = source->getblkno;
    struct XdeltaBlock *slot = NULL;
    int i;
    for (i = 0; i < XDELTA_BLOCKS; i++)
        if (blocks[i].bytes && blocks[i].number == number) slot = &blocks[i];
    if (!slot)
    {
        unsigned long long offset = (unsigned long long)number << XDELTA_BLOCK_SHIFT;
        long got;
        slot = &blocks[0];
        for (i = 1; i < XDELTA_BLOCKS; i++)
            if (blocks[i].used < slot->used) slot = &blocks[i];
        if (!slot->bytes && !(slot->bytes = (unsigned char *)malloc((size_t)1 << XDELTA_BLOCK_SHIFT)))
            return 0;
        slot->number = (xoff_t)-1;
        if (offset >= io->sourceBytes) got = 0;
        else
        {
            unsigned long long want = io->sourceBytes - offset;
            if (want > ((unsigned long long)1 << XDELTA_BLOCK_SHIFT)) want = (unsigned long long)1 << XDELTA_BLOCK_SHIFT;
            got = io->readSource(io->context, offset, slot->bytes, (size_t)want);
            if (got > (long)want) got = (long)want;
        }
        if (got < 0) return 0;
        if ((unsigned long long)got < ((unsigned long long)1 << XDELTA_BLOCK_SHIFT) &&
            offset + (unsigned long long)got < io->sourceBytes) return 0;
        slot->number = number;
        slot->size = (usize_t)got;
    }
    slot->used = ++*clock;
    source->curblkno = number;
    source->onblk = slot->size;
    source->curblk = slot->bytes;
    return 1;
}

int NativeXdelta_Decode(const struct NativeXdeltaIo *io, char *error, size_t errorSize)
{
    xd3_stream stream;
    xd3_config config;
    xd3_source source;
    struct XdeltaBlock blocks[XDELTA_BLOCKS];
    unsigned char *input = NULL;
    unsigned long long clock = 0;
    int result = NATIVE_XDELTA_FAILED, eof = 0, started = 0, ret;
    xdelta_error(error, errorSize, "");
    memset(blocks, 0, sizeof blocks);
    memset(&stream, 0, sizeof stream);
    memset(&source, 0, sizeof source);
    if (!io || !io->readPatch || !io->readSource || !io->writeTarget)
    {
        xdelta_error(error, errorSize, "Expected patch, source and target callbacks");
        return NATIVE_XDELTA_FAILED;
    }
    if (!(input = (unsigned char *)malloc(XDELTA_INPUT_CHUNK)))
    {
        xdelta_error(error, errorSize, "Out of memory");
        return NATIVE_XDELTA_FAILED;
    }
    xd3_init_config(&config, 0);
    config.winsize = XDELTA_INPUT_CHUNK;
    if (xd3_config_stream(&stream, &config))
    {
        xdelta_error(error, errorSize, stream.msg ? stream.msg : "Cannot configure decoder");
        goto done;
    }
    source.blksize = (usize_t)1 << XDELTA_BLOCK_SHIFT;
    source.name = "disc";
    source.curblkno = (xoff_t)-1;
    if (xd3_set_source_and_size(&stream, &source, (xoff_t)io->sourceBytes))
    {
        xdelta_error(error, errorSize, stream.msg ? stream.msg : "Cannot attach the disc");
        goto done;
    }
    for (;;)
    {
        long got;
        if (io->cancel && __atomic_load_n((const int *)io->cancel, __ATOMIC_ACQUIRE)) { result = NATIVE_XDELTA_CANCELLED; xdelta_error(error, errorSize, "Cancelled"); goto done; }
        got = io->readPatch(io->context, input, XDELTA_INPUT_CHUNK);
        if (got < 0) { xdelta_error(error, errorSize, "Cannot read the patch file"); goto done; }
        if (!started)
        {
            int header = NativeXdelta_CheckHeader(input, (size_t)got);
            if (header != NATIVE_XDELTA_OK)
            {
                result = header;
                xdelta_error(error, errorSize, header == NATIVE_XDELTA_UNSUPPORTED ?
                    "Unsupported patch compression" : "Not an xdelta patch");
                goto done;
            }
            started = 1;
        }
        if (got == 0) { eof = 1; xd3_set_flags(&stream, XD3_FLUSH | stream.flags); }
        xd3_avail_input(&stream, input, (usize_t)got);
        for (;;)
        {
            if (io->cancel && __atomic_load_n((const int *)io->cancel, __ATOMIC_ACQUIRE)) { result = NATIVE_XDELTA_CANCELLED; xdelta_error(error, errorSize, "Cancelled"); goto done; }
            ret = xd3_decode_input(&stream);
            if (ret == XD3_INPUT) break;
            if (ret == XD3_OUTPUT)
            {
                if (stream.avail_out && !io->writeTarget(io->context, stream.next_out, stream.avail_out))
                {
                    result = NATIVE_XDELTA_STOPPED;
                    xdelta_error(error, errorSize, "Stopped");
                    goto done;
                }
                xd3_consume_output(&stream);
                continue;
            }
            if (ret == XD3_GETSRCBLK)
            {
                if (source.getblkno > (xoff_t)(io->sourceBytes >> XDELTA_BLOCK_SHIFT))
                {
                    result = NATIVE_XDELTA_SOURCE_RANGE;
                    xdelta_error(error, errorSize, "This patch is for a different disc");
                    goto done;
                }
                if (!xdelta_get_block(io, &source, blocks, &clock))
                {
                    xdelta_error(error, errorSize, "Cannot read the disc");
                    goto done;
                }
                continue;
            }
            if (ret == XD3_WINFINISH && !(stream.dec_win_ind & VCD_ADLER32))
            {
                /* Every window must prove its bytes (design: integrity). */
                result = NATIVE_XDELTA_NO_CHECKSUM;
                xdelta_error(error, errorSize, "Patch has no checksums");
                goto done;
            }
            if (ret == XD3_GOTHEADER || ret == XD3_WINSTART || ret == XD3_WINFINISH) continue;
            if (stream.msg && strstr(stream.msg, "checksum mismatch"))
            {
                result = NATIVE_XDELTA_CHECKSUM;
                xdelta_error(error, errorSize, "Patch does not match this disc");
            }
            else if (stream.msg && strstr(stream.msg, "secondary"))
            {
                result = NATIVE_XDELTA_UNSUPPORTED;
                xdelta_error(error, errorSize, "Unsupported patch compression");
            }
            else
            {
                char text[160];
                snprintf(text, sizeof text, "Damaged patch (%s)", stream.msg ? stream.msg : xd3_strerror(ret));
                xdelta_error(error, errorSize, text);
            }
            goto done;
        }
        if (eof) break;
    }
    if (xd3_close_stream(&stream))
    {
        xdelta_error(error, errorSize, "Damaged patch (truncated)");
        goto done;
    }
    result = NATIVE_XDELTA_OK;
done:
    {
        int i;
        xd3_free_stream(&stream);
        for (i = 0; i < XDELTA_BLOCKS; i++) free(blocks[i].bytes);
        free(input);
    }
    return result;
}
