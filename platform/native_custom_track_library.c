#include <platform/native_custom_track_library.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static size_t library_codepoint(const unsigned char *text)
{
    size_t length, i;
    unsigned int scalar;
    if (*text < 0x80) return 1;
    if (*text >= 0xC2 && *text <= 0xDF) { length = 2; scalar = *text & 31; }
    else if (*text >= 0xE0 && *text <= 0xEF) { length = 3; scalar = *text & 15; }
    else if (*text >= 0xF0 && *text <= 0xF4) { length = 4; scalar = *text & 7; }
    else return 0;
    for (i = 1; i < length; i++)
    {
        if ((text[i] & 0xC0) != 0x80) return 0;
        scalar = (scalar << 6) | (text[i] & 63);
    }
    if ((length == 3 && scalar < 0x800) || (length == 4 && scalar < 0x10000) ||
        scalar > 0x10FFFF || (scalar >= 0xD800 && scalar <= 0xDFFF)) return 0;
    return length;
}

size_t CustomTrackLibrary_TextPage(const char *text, size_t columns, size_t page,
                                 char *output, size_t outputSize)
{
    const unsigned char *cursor;
    size_t count = 0, pages, skip, used = 0;
    if (output && outputSize) output[0] = 0;
    if (!text || !columns) return 0;
    for (cursor = (const unsigned char *)text; *cursor; count++)
    {
        size_t length = library_codepoint(cursor);
        cursor += length ? length : 1;
    }
    pages = count ? 1 + (count - 1) / columns : 1;
    skip = (page % pages) * columns;
    cursor = (const unsigned char *)text;
    while (skip--)
    {
        size_t length = library_codepoint(cursor);
        cursor += length ? length : 1;
    }
    if (!output || !outputSize) return pages;
    while (*cursor && columns--)
    {
        size_t length = library_codepoint(cursor), bytes = length ? length : 1;
        if (bytes >= outputSize - used) break;
        if (length) memcpy(output + used, cursor, length);
        else output[used] = '?';
        used += bytes;
        cursor += bytes;
    }
    output[used] = 0;
    return pages;
}

static unsigned char library_fold(unsigned char c)
{
    return c >= 'A' && c <= 'Z' ? (unsigned char)(c + 'a' - 'A') : c;
}

static int library_compare_text(const char *a, const char *b)
{
    while (*a && library_fold((unsigned char)*a) == library_fold((unsigned char)*b))
    { a++; b++; }
    return (int)library_fold((unsigned char)*a) - (int)library_fold((unsigned char)*b);
}

static int library_contains(const char *text, const char *query)
{
    const char *a, *b;
    if (!*query) return 1;
    for (; *text; text++)
    {
        for (a = text, b = query; *a && *b && library_fold((unsigned char)*a) == library_fold((unsigned char)*b); a++, b++) {}
        if (!*b) return 1;
    }
    return 0;
}

static int library_order(const void *a, const void *b)
{
    const struct CustomTrackLibraryEntry *left = a, *right = b;
    unsigned char lc = library_fold((unsigned char)left->title[0]);
    unsigned char rc = library_fold((unsigned char)right->title[0]);
    int ll = lc >= 'a' && lc <= 'z', rl = rc >= 'a' && rc <= 'z';
    int result = ll - rl; /* Non-letter titles form the leading # bucket. */
    if (!result) result = library_compare_text(left->title, right->title);
    if (!result) result = strcmp(left->version, right->version);
    if (!result) result = strcmp(left->manifestSha256, right->manifestSha256);
    return result;
}

size_t CustomTrackLibrary_PageCount(const struct CustomTrackLibrary *library)
{
    return library && library->visibleCount ?
        (library->visibleCount + CTR_LIBRARY_PAGE_ROWS - 1) / CTR_LIBRARY_PAGE_ROWS : 1;
}

size_t CustomTrackLibrary_PageRowCount(const struct CustomTrackLibrary *library, size_t page)
{
    size_t first = page * CTR_LIBRARY_PAGE_ROWS, left;
    if (!library || page >= CustomTrackLibrary_PageCount(library) || first >= library->visibleCount) return 0;
    left = library->visibleCount - first;
    return left < CTR_LIBRARY_PAGE_ROWS ? left : CTR_LIBRARY_PAGE_ROWS;
}

static char library_initial(const char *title)
{
    unsigned char c = library_fold((unsigned char)title[0]);
    return c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : '#';
}

void CustomTrackLibrary_PageLabel(const struct CustomTrackLibrary *library, size_t page,
                                 char *output, size_t outputSize)
{
    size_t count = CustomTrackLibrary_PageRowCount(library, page);
    char first, last;
    if (!output || !outputSize) return;
    if (!count) { snprintf(output, outputSize, "Custom"); return; }
    first = library_initial(CustomTrackLibrary_Row(library, page * CTR_LIBRARY_PAGE_ROWS)->title);
    last = library_initial(CustomTrackLibrary_Row(library, page * CTR_LIBRARY_PAGE_ROWS + count - 1)->title);
    if (first == last) snprintf(output, outputSize, "%c", first);
    else snprintf(output, outputSize, "%c-%c", first, last);
}

static int library_valid(const struct CustomTrackLibraryEntry *entry)
{
    size_t i;
    if (entry->manifestSha256[64] != 0 || !entry->title[0] || !entry->version[0] ||
        !memchr(entry->title, 0, sizeof entry->title) ||
        !memchr(entry->version, 0, sizeof entry->version) ||
        !memchr(entry->author, 0, sizeof entry->author) ||
        !memchr(entry->disabledReason, 0, sizeof entry->disabledReason) ||
        !memchr(entry->local.uuid, 0, sizeof entry->local.uuid) ||
        !memchr(entry->local.author, 0, sizeof entry->local.author) ||
        (entry->local.arcade != 0 && entry->local.arcade != 1) ||
        (entry->local.timeTrial != 0 && entry->local.timeTrial != 1) ||
        !memchr(entry->local.arcadeReason, 0, sizeof entry->local.arcadeReason) ||
        !memchr(entry->local.timeTrialReason, 0, sizeof entry->local.timeTrialReason) ||
        !memchr(entry->local.levSha256, 0, sizeof entry->local.levSha256) ||
        !memchr(entry->local.vrmSha256, 0, sizeof entry->local.vrmSha256)) return 0;
    for (i = 0; i < 64; i++)
        if (!((entry->manifestSha256[i] >= '0' && entry->manifestSha256[i] <= '9') ||
              (entry->manifestSha256[i] >= 'a' && entry->manifestSha256[i] <= 'f'))) return 0;
    if ((entry->installed != 0 && entry->installed != 1) ||
        (entry->requiredBySeed != 0 && entry->requiredBySeed != 1)) return 0;
    return 1;
}

static void library_refresh(struct CustomTrackLibrary *library)
{
    size_t i;
    library->visibleCount = 0;
    library->selected = 0;
    for (i = 0; i < library->count; i++)
    {
        const struct CustomTrackLibraryEntry *entry = &library->entries[i];
        if (library->requiredOnly && !entry->requiredBySeed) continue;
        if (library->modeFilter && !(entry->supportedModes & library->modeFilter)) continue;
        if (!library_contains(entry->title, library->query) &&
            !library_contains(entry->author, library->query) &&
            !library_contains(entry->version, library->query)) continue;
        library->visible[library->visibleCount] = i;
        if (!strcmp(entry->manifestSha256, library->rememberedPin))
            library->selected = library->visibleCount;
        library->visibleCount++;
    }
}

int CustomTrackLibrary_Replace(struct CustomTrackLibrary *library,
                              const struct CustomTrackLibraryEntry *entries, size_t count)
{
    struct CustomTrackLibraryEntry *copy = NULL;
    size_t i, j;
    if (!library || count > CTR_LIBRARY_MAX || (count && !entries)) return 0;
    for (i = 0; i < count; i++)
    {
        if (!library_valid(&entries[i])) return 0;
        for (j = 0; j < i; j++)
            if (!strcmp(entries[i].manifestSha256, entries[j].manifestSha256)) return 0;
    }
    if (count)
    {
        copy = malloc(count * sizeof *copy);
        if (!copy) return 0;
        memcpy(copy, entries, count * sizeof *copy);
        qsort(copy, count, sizeof *copy, library_order);
    }
    free(library->entries);
    library->entries = copy;
    library->count = count;
    library_refresh(library);
    if (!library->rememberedPin[0] && library->visibleCount)
        memcpy(library->rememberedPin, CustomTrackLibrary_Selected(library)->manifestSha256, 65);
    return 1;
}

void CustomTrackLibrary_Free(struct CustomTrackLibrary *library)
{
    if (!library) return;
    free(library->entries);
    memset(library, 0, sizeof *library);
}

int CustomTrackLibrary_MarkInstalled(struct CustomTrackLibrary *library, const char *pin)
{
    size_t i;
    if (!library || !pin) return 0;
    for (i = 0; i < library->count; i++)
        if (!strcmp(library->entries[i].manifestSha256, pin))
        {
            library->entries[i].installed = 1;
            /* Installation cannot upgrade or preserve an unverified mode claim. */
            library->entries[i].supportedModes = 0;
            snprintf(library->entries[i].disabledReason, sizeof library->entries[i].disabledReason,
                     "Installed exact revision; gameplay certification and launch integration pending");
            library_refresh(library);
            return 1;
        }
    return 0;
}

int CustomTrackLibrary_ApplyStore(struct CustomTrackLibrary *library,
    const struct CustomPackageStoreEntry *entries, size_t count)
{
    struct CustomTrackLibraryEntry *merged;
    size_t total, i, j, k;
    int result;
    if (!library || count > CTR_LIBRARY_MAX || (count && !entries)) return 0;
    merged = calloc(CTR_LIBRARY_MAX, sizeof *merged);
    if (!merged) return 0;
    total = library->count;
    if (total) memcpy(merged, library->entries, total * sizeof *merged);
    for (i = 0; i < total; i++)
    {
        if (merged[i].installed)
            snprintf(merged[i].disabledReason, sizeof merged[i].disabledReason, "Installed revision absent at last store scan");
        merged[i].installed = 0;
        merged[i].supportedModes = 0;
        memset(&merged[i].local, 0, sizeof merged[i].local);
    }
    for (i = 0; i < count; i++)
    {
        const struct CustomPackageStoreEntry *source = &entries[i];
        if (source->pin[64] || (source->verified != 0 && source->verified != 1) ||
            !memchr(source->error, 0, sizeof source->error)) goto fail;
        for (k = 0; k < i; k++) if (!strcmp(entries[k].pin, source->pin)) goto fail;
        for (j = 0; j < total; j++) if (!strcmp(merged[j].manifestSha256, source->pin)) break;
        if (j == total)
        {
            if (total == CTR_LIBRARY_MAX) goto fail;
            memcpy(merged[j].manifestSha256, source->pin, 65);
            strcpy(merged[j].title, "Unavailable package revision");
            strcpy(merged[j].version, "unknown");
            total++;
        }
        if (source->verified)
        {
            if (source->manifest.sha256[64] || strcmp(source->manifest.sha256, source->pin)) goto fail;
            memcpy(merged[j].title, source->manifest.title, sizeof merged[j].title);
            memcpy(merged[j].version, source->manifest.version, sizeof merged[j].version);
            merged[j].installed = 1;
            merged[j].local = source->local;
            if (source->local.author[0]) memcpy(merged[j].author, source->local.author, sizeof merged[j].author);
            snprintf(merged[j].disabledReason, sizeof merged[j].disabledReason,
                     "Installed exact revision; gameplay certification and launch integration pending");
        }
        else snprintf(merged[j].disabledReason, sizeof merged[j].disabledReason, "%s",
                      source->error[0] ? source->error : "Installed revision failed verification");
    }
    result = CustomTrackLibrary_Replace(library, merged, total);
    free(merged);
    return result;
fail:
    free(merged);
    return 0;
}

int CustomTrackLibrary_Filter(struct CustomTrackLibrary *library, const char *query,
                             unsigned int mode, int requiredOnly)
{
    if (!library || !query || strlen(query) > CTR_LIBRARY_QUERY_MAX ||
        (requiredOnly != 0 && requiredOnly != 1)) return 0;
    memmove(library->query, query, strlen(query) + 1);
    library->modeFilter = mode;
    library->requiredOnly = requiredOnly;
    library_refresh(library);
    return 1;
}

void CustomTrackLibrary_Move(struct CustomTrackLibrary *library, int direction)
{
    if (!library || !library->visibleCount || !direction) return;
    if (direction > 0) library->selected = (library->selected + 1) % library->visibleCount;
    else library->selected = library->selected ? library->selected - 1 : library->visibleCount - 1;
    memcpy(library->rememberedPin, CustomTrackLibrary_Selected(library)->manifestSha256, 65);
}

const struct CustomTrackLibraryEntry *CustomTrackLibrary_Row(const struct CustomTrackLibrary *library, size_t row)
{
    if (!library || row >= library->visibleCount) return NULL;
    return &library->entries[library->visible[row]];
}

const struct CustomTrackLibraryEntry *CustomTrackLibrary_Selected(const struct CustomTrackLibrary *library)
{
    return library ? CustomTrackLibrary_Row(library, library->selected) : NULL;
}

static unsigned int library_read32(const unsigned char *bytes)
{
    return (unsigned int)bytes[0] | (unsigned int)bytes[1] << 8 |
           (unsigned int)bytes[2] << 16 | (unsigned int)bytes[3] << 24;
}

static int library_read_text(const unsigned char *bytes, size_t size, size_t *offset,
                             char *destination, size_t capacity)
{
    unsigned int length;
    size_t i;
    if (*offset > size || size - *offset < 4) return 0;
    length = library_read32(bytes + *offset);
    *offset += 4;
    if (length >= capacity || length > size - *offset) return 0;
    for (i = 0; i < length; i++)
        if (bytes[*offset + i] < 32 || bytes[*offset + i] == 127) return 0;
    memcpy(destination, bytes + *offset, length);
    destination[length] = 0;
    *offset += length;
    return 1;
}

int CustomTrackLibrary_ParseCatalogue(struct CustomTrackLibrary *library, const unsigned char *bytes, size_t size)
{
    struct CustomTrackLibraryEntry *entries;
    unsigned int count, i;
    size_t offset = 12;
    int valid = 0;
    if (!library || !bytes || size < 12 || size > 1024 * 1024 || memcmp(bytes, "CTRLB001", 8)) return 0;
    count = library_read32(bytes + 8);
    if (count > CTR_LIBRARY_MAX) return 0;
    entries = calloc(count ? count : 1, sizeof *entries);
    if (!entries) return 0;
    for (i = 0; i < count; i++)
    {
        struct CustomTrackLibraryEntry *entry = &entries[i];
        if (!library_read_text(bytes, size, &offset, entry->manifestSha256, sizeof entry->manifestSha256) ||
            !library_read_text(bytes, size, &offset, entry->title, sizeof entry->title) ||
            !library_read_text(bytes, size, &offset, entry->version, sizeof entry->version) ||
            !library_read_text(bytes, size, &offset, entry->author, sizeof entry->author)) goto done;
        strcpy(entry->disabledReason, "Catalogue only: local verification and mode review required");
    }
    if (offset != size) goto done;
    valid = CustomTrackLibrary_Replace(library, entries, count);
done:
    free(entries);
    return valid;
}

int CustomTrackLibrary_ReadCatalogue(struct CustomTrackLibrary *library, const char *path)
{
    FILE *file;
    unsigned char *bytes;
    size_t size;
    int valid;
    if (!library || !path) return 0;
    file = fopen(path, "rb");
    if (!file) return 0;
    bytes = malloc(1024 * 1024 + 1);
    if (!bytes) { fclose(file); return 0; }
    size = fread(bytes, 1, 1024 * 1024 + 1, file);
    valid = !ferror(file) && CustomTrackLibrary_ParseCatalogue(library, bytes, size);
    fclose(file);
    free(bytes);
    return valid;
}
