/* Time Trial records for custom tracks: see include/platform/native_custom_records.h. */
#include <platform/native_custom_records.h>
#include <platform/native_sha256.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

#define RECORDS_TIMES_HEADER "CTR custom track records 1"
#define RECORDS_GHOST_MAGIC "CTRCGHO1"
#define RECORDS_GHOST_HEAD (8 + 36 + 64 + 64 + 4 + 4 + 64)
#define RECORDS_TIMES_TEXT_MAX 1024

static int records_hex(const char *s, size_t n)
{
    size_t i;
    if (strlen(s) != n) return 0;
    for (i = 0; i < n; i++)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) return 0;
    return 1;
}

static int records_uuid(const char *s)
{
    size_t i;
    if (strlen(s) != 36) return 0;
    for (i = 0; i < 36; i++)
    {
        int dash = i == 8 || i == 13 || i == 18 || i == 23;
        if (dash ? s[i] != '-' : !((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) return 0;
    }
    return 1;
}

static int records_key_valid(const struct CustomRecordKey *k)
{
    return k && memchr(k->uuid, 0, sizeof k->uuid) && memchr(k->lev, 0, sizeof k->lev) &&
        memchr(k->vrm, 0, sizeof k->vrm) && records_uuid(k->uuid) && records_hex(k->lev, 64) &&
        records_hex(k->vrm, 64) && k->laps >= 1 && k->laps <= 7;
}

int CustomRecords_MakeKey(struct CustomRecordKey *key, const char *uuid, const char *lev,
                          const char *vrm, unsigned int laps)
{
    if (!key) return 0;
    memset(key, 0, sizeof *key);
    if (!uuid || !lev || !vrm || strlen(uuid) != 36 || strlen(lev) != 64 || strlen(vrm) != 64) return 0;
    memcpy(key->uuid, uuid, 36);
    memcpy(key->lev, lev, 64);
    memcpy(key->vrm, vrm, 64);
    key->laps = laps;
    if (records_key_valid(key)) return 1;
    memset(key, 0, sizeof *key);
    return 0;
}

int CustomRecords_Stem(const struct CustomRecordKey *key, char *out, size_t cap)
{
    int n;
    if (!out || !cap) return 0;
    out[0] = 0;
    if (!records_key_valid(key)) return 0;
    n = snprintf(out, cap, "%s-%.16s-%.16s-%ul", key->uuid, key->lev, key->vrm, key->laps);
    if (n < 0 || (size_t)n >= cap) { out[0] = 0; return 0; }
    return 1;
}

/* Names come from the engine's name entry; keep them printable and one line. */
static void records_clean_name(char *dst, const char *src)
{
    size_t i;
    for (i = 0; i + 1 < CTR_RECORDS_NAME && src[i]; i++)
        dst[i] = (src[i] >= 32 && src[i] < 127) ? src[i] : '?';
    dst[i] = 0;
}

size_t CustomRecords_FormatTimes(const struct CustomRecordKey *key, const struct CustomRecordTimes *times,
                                 char *out, size_t cap)
{
    size_t used;
    int i, n;
    if (!out || !cap) return 0;
    out[0] = 0;
    if (!records_key_valid(key) || !times) return 0;
    n = snprintf(out, cap, "%s\nuuid %s\nlev %s\nvrm %s\nlaps %u\n", RECORDS_TIMES_HEADER,
                 key->uuid, key->lev, key->vrm, key->laps);
    if (n < 0 || (size_t)n >= cap) { out[0] = 0; return 0; }
    used = (size_t)n;
    for (i = 0; i < CTR_RECORDS_ENTRIES; i++)
    {
        char name[CTR_RECORDS_NAME];
        char tag[8];
        const struct CustomRecordEntry *e = &times->entry[i];
        if (!memchr(e->name, 0, sizeof e->name) || e->characterID > 0xffff) { out[0] = 0; return 0; }
        records_clean_name(name, e->name);
        if (i) snprintf(tag, sizeof tag, "race%d", i); else snprintf(tag, sizeof tag, "lap");
        n = snprintf(out + used, cap - used, "%s %u %u %s\n", tag, e->time, e->characterID, name);
        if (n < 0 || (size_t)n >= cap - used) { out[0] = 0; return 0; }
        used += (size_t)n;
    }
    return used;
}

/* Next line without its newline; *at advances past it. */
static int records_line(const char *text, size_t size, size_t *at, char *line, size_t cap)
{
    size_t n = 0;
    if (*at >= size) return 0;
    while (*at < size && text[*at] != '\n')
    {
        if (n + 1 >= cap || text[*at] == 0) return 0;
        line[n++] = text[(*at)++];
    }
    if (*at >= size) return 0; /* every line ends in a newline */
    (*at)++;
    if (n && line[n - 1] == '\r') n--;
    line[n] = 0;
    return 1;
}

static int records_unsigned(const char **p, unsigned int *out)
{
    unsigned long v = 0;
    const char *s = *p;
    if (*s < '0' || *s > '9') return 0;
    while (*s >= '0' && *s <= '9')
    {
        v = v * 10 + (unsigned long)(*s - '0');
        if (v > 0xffffffffUL) return 0;
        s++;
    }
    *out = (unsigned int)v;
    *p = s;
    return 1;
}

int CustomRecords_ParseTimes(const struct CustomRecordKey *key, const char *text, size_t size,
                             struct CustomRecordTimes *out)
{
    char line[256], expect[160];
    struct CustomRecordTimes parsed;
    size_t at = 0;
    int i;
    if (!out || !text || !records_key_valid(key)) return 0;
    memset(&parsed, 0, sizeof parsed);
    if (!records_line(text, size, &at, line, sizeof line) || strcmp(line, RECORDS_TIMES_HEADER)) return 0;
    snprintf(expect, sizeof expect, "uuid %s", key->uuid);
    if (!records_line(text, size, &at, line, sizeof line) || strcmp(line, expect)) return 0;
    snprintf(expect, sizeof expect, "lev %s", key->lev);
    if (!records_line(text, size, &at, line, sizeof line) || strcmp(line, expect)) return 0;
    snprintf(expect, sizeof expect, "vrm %s", key->vrm);
    if (!records_line(text, size, &at, line, sizeof line) || strcmp(line, expect)) return 0;
    snprintf(expect, sizeof expect, "laps %u", key->laps);
    if (!records_line(text, size, &at, line, sizeof line) || strcmp(line, expect)) return 0;
    for (i = 0; i < CTR_RECORDS_ENTRIES; i++)
    {
        char tag[8];
        const char *p;
        size_t tagLen, nameLen;
        struct CustomRecordEntry *e = &parsed.entry[i];
        if (i) snprintf(tag, sizeof tag, "race%d", i); else snprintf(tag, sizeof tag, "lap");
        tagLen = strlen(tag);
        if (!records_line(text, size, &at, line, sizeof line) || strncmp(line, tag, tagLen) || line[tagLen] != ' ')
            return 0;
        p = line + tagLen + 1;
        if (!records_unsigned(&p, &e->time) || *p++ != ' ' || !records_unsigned(&p, &e->characterID) ||
            e->characterID > 0xffff || *p++ != ' ')
            return 0;
        nameLen = strlen(p);
        if (nameLen >= CTR_RECORDS_NAME) return 0;
        for (size_t k = 0; k < nameLen; k++)
            if (p[k] < 32 || p[k] >= 127) return 0;
        memcpy(e->name, p, nameLen + 1);
    }
    if (at != size) return 0;
    *out = parsed;
    return 1;
}

static void records_put32(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}

static unsigned int records_get32(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

static void records_digest(const void *data, size_t n, char hex[NATIVE_SHA256_HEX_BYTES])
{
    struct NativeSha256Ctx ctx;
    unsigned char digest[NATIVE_SHA256_DIGEST_BYTES];
    (void)NativeSha256_HexEquals;
    NativeSha256_Init(&ctx);
    NativeSha256_Update(&ctx, data, n);
    NativeSha256_Final(&ctx, digest);
    NativeSha256_ToHex(digest, hex);
}

size_t CustomRecords_GhostFileMax(void) { return RECORDS_GHOST_HEAD + CTR_RECORDS_GHOST_MAX; }

size_t CustomRecords_FormatGhost(const struct CustomRecordKey *key, const void *ghost, size_t ghostBytes,
                                 unsigned char *out, size_t cap)
{
    char hex[NATIVE_SHA256_HEX_BYTES];
    unsigned char *p = out;
    if (!records_key_valid(key) || !ghost || !out || !ghostBytes || ghostBytes > CTR_RECORDS_GHOST_MAX ||
        cap < RECORDS_GHOST_HEAD + ghostBytes) return 0;
    records_digest(ghost, ghostBytes, hex);
    memcpy(p, RECORDS_GHOST_MAGIC, 8); p += 8;
    memcpy(p, key->uuid, 36); p += 36;
    memcpy(p, key->lev, 64); p += 64;
    memcpy(p, key->vrm, 64); p += 64;
    records_put32(p, key->laps); p += 4;
    records_put32(p, (unsigned int)ghostBytes); p += 4;
    memcpy(p, hex, 64); p += 64;
    memcpy(p, ghost, ghostBytes);
    return RECORDS_GHOST_HEAD + ghostBytes;
}

int CustomRecords_ParseGhost(const struct CustomRecordKey *key, const unsigned char *file, size_t size,
                             void *ghost, size_t cap, size_t *ghostBytes)
{
    char hex[NATIVE_SHA256_HEX_BYTES];
    const unsigned char *p = file;
    size_t bytes;
    if (ghostBytes) *ghostBytes = 0;
    if (!records_key_valid(key) || !file || !ghost || size < RECORDS_GHOST_HEAD) return 0;
    if (memcmp(p, RECORDS_GHOST_MAGIC, 8)) return 0;
    p += 8;
    if (memcmp(p, key->uuid, 36)) return 0;
    p += 36;
    if (memcmp(p, key->lev, 64)) return 0;
    p += 64;
    if (memcmp(p, key->vrm, 64)) return 0;
    p += 64;
    if (records_get32(p) != key->laps) return 0;
    p += 4;
    bytes = records_get32(p);
    p += 4;
    if (!bytes || bytes > CTR_RECORDS_GHOST_MAX || bytes > cap || size != RECORDS_GHOST_HEAD + bytes) return 0;
    records_digest(file + RECORDS_GHOST_HEAD, bytes, hex);
    if (memcmp(p, hex, 64)) return 0;
    memcpy(ghost, file + RECORDS_GHOST_HEAD, bytes);
    if (ghostBytes) *ghostBytes = bytes;
    return 1;
}

static int records_path(const char *dir, const struct CustomRecordKey *key, const char *ext, char *out, size_t cap)
{
    char stem[CTR_RECORDS_STEM_MAX];
    int n;
    if (!dir || !CustomRecords_Stem(key, stem, sizeof stem)) return 0;
    n = snprintf(out, cap, "%s/%s%s", dir, stem, ext);
    return n > 0 && (size_t)n < cap;
}

static int records_read(const char *path, unsigned char *buffer, size_t cap, size_t *size)
{
    FILE *f = fopen(path, "rb");
    size_t n;
    int extra;
    *size = 0;
    if (!f) return 0;
    n = fread(buffer, 1, cap, f);
    extra = fgetc(f);
    fclose(f);
    if (extra != EOF) return 0; /* larger than any valid file */
    *size = n;
    return 1;
}

static int records_write(const char *dir, const char *path, const void *data, size_t size)
{
    char tmp[512];
    FILE *f;
    int ok;
#ifdef _WIN32
    if (_mkdir(dir) != 0 && errno != EEXIST) return 0;
#else
    if (mkdir(dir, 0755) != 0 && errno != EEXIST) return 0;
#endif
    if (snprintf(tmp, sizeof tmp, "%s.tmp", path) >= (int)sizeof tmp) return 0;
    f = fopen(tmp, "wb");
    if (!f) return 0;
    ok = fwrite(data, 1, size, f) == size;
    ok = fflush(f) == 0 && ok;
    ok = fclose(f) == 0 && ok;
    if (!ok) { remove(tmp); return 0; }
#ifdef _WIN32
    ok = MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    ok = rename(tmp, path) == 0;
#endif
    if (!ok) remove(tmp);
    return ok;
}

int CustomRecords_LoadTimes(const char *dir, const struct CustomRecordKey *key, struct CustomRecordTimes *out)
{
    char path[512];
    unsigned char text[RECORDS_TIMES_TEXT_MAX];
    size_t size;
    if (!out || !records_path(dir, key, ".times", path, sizeof path) ||
        !records_read(path, text, sizeof text, &size)) return 0;
    return CustomRecords_ParseTimes(key, (const char *)text, size, out);
}

int CustomRecords_SaveTimes(const char *dir, const struct CustomRecordKey *key, const struct CustomRecordTimes *times)
{
    char path[512], text[RECORDS_TIMES_TEXT_MAX];
    size_t n;
    if (!records_path(dir, key, ".times", path, sizeof path)) return 0;
    n = CustomRecords_FormatTimes(key, times, text, sizeof text);
    return n && records_write(dir, path, text, n);
}

int CustomRecords_LoadGhost(const char *dir, const struct CustomRecordKey *key, void *ghost, size_t cap,
                            size_t *ghostBytes)
{
    char path[512];
    unsigned char *file;
    size_t size;
    int ok = 0;
    if (ghostBytes) *ghostBytes = 0;
    if (!records_path(dir, key, ".ghost", path, sizeof path)) return 0;
    file = malloc(CustomRecords_GhostFileMax());
    if (!file) return 0;
    if (records_read(path, file, CustomRecords_GhostFileMax(), &size))
        ok = CustomRecords_ParseGhost(key, file, size, ghost, cap, ghostBytes);
    free(file);
    return ok;
}

int CustomRecords_SaveGhost(const char *dir, const struct CustomRecordKey *key, const void *ghost, size_t ghostBytes)
{
    char path[512];
    unsigned char *file;
    size_t n;
    int ok = 0;
    if (!records_path(dir, key, ".ghost", path, sizeof path)) return 0;
    file = malloc(CustomRecords_GhostFileMax());
    if (!file) return 0;
    n = CustomRecords_FormatGhost(key, ghost, ghostBytes, file, CustomRecords_GhostFileMax());
    if (n) ok = records_write(dir, path, file, n);
    free(file);
    return ok;
}
