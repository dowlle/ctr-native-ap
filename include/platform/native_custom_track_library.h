#ifndef NATIVE_CUSTOM_TRACK_LIBRARY_H
#define NATIVE_CUSTOM_TRACK_LIBRARY_H

#include <stddef.h>
#include <platform/native_custom_package.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Cached UI projection, never loader or certification authority. */
#define CTR_LIBRARY_MAX 1024
#define CTR_LIBRARY_QUERY_MAX 80
#define CTR_LIBRARY_PAGE_ROWS 8

struct CustomTrackLibraryEntry
{
    char manifestSha256[65];
    char title[641];
    char version[65];
    char author[81];
    char disabledReason[257];
    unsigned int supportedModes; /* Derived elsewhere from verified evidence. */
    int installed;
    int requiredBySeed;
    struct CustomPackageLocalInfo local;
};

struct CustomTrackLibrary
{
    struct CustomTrackLibraryEntry *entries;
    size_t count;
    size_t visible[CTR_LIBRARY_MAX];
    size_t visibleCount;
    size_t selected;
    char rememberedPin[65];
    char query[CTR_LIBRARY_QUERY_MAX + 1];
    unsigned int modeFilter;
    int requiredOnly;
};

/* Zero-initialize before first use. Replace copies data transactionally, so
   publisher/worker buffers can be freed immediately. No download or I/O here. */
int CustomTrackLibrary_Replace(struct CustomTrackLibrary *library,
                              const struct CustomTrackLibraryEntry *entries, size_t count);
void CustomTrackLibrary_Free(struct CustomTrackLibrary *library);
/* Import a metadata-only CTRLB001 cache. All rows start uninstalled and
   uncertified. These functions never authorize content or change seed pins. */
int CustomTrackLibrary_ReadCatalogue(struct CustomTrackLibrary *library, const char *path);
int CustomTrackLibrary_ParseCatalogue(struct CustomTrackLibrary *library, const unsigned char *bytes, size_t size);
int CustomTrackLibrary_Filter(struct CustomTrackLibrary *library, const char *query,
                             unsigned int mode, int requiredOnly);
void CustomTrackLibrary_Move(struct CustomTrackLibrary *library, int direction);
/* UI projection of a completed exact-pin install, never mode certification. */
int CustomTrackLibrary_MarkInstalled(struct CustomTrackLibrary *library, const char *pin);
int CustomTrackLibrary_ApplyStore(struct CustomTrackLibrary *library,
    const struct CustomPackageStoreEntry *entries, size_t count);
const struct CustomTrackLibraryEntry *CustomTrackLibrary_Selected(const struct CustomTrackLibrary *library);
const struct CustomTrackLibraryEntry *CustomTrackLibrary_Row(const struct CustomTrackLibrary *library, size_t row);
/* Custom pages only; the game owns the preceding Vanilla page. Navigation
   wraps within a page, retaining each page's row in the caller. */
size_t CustomTrackLibrary_PageCount(const struct CustomTrackLibrary *library);
size_t CustomTrackLibrary_PageRowCount(const struct CustomTrackLibrary *library, size_t page);
void CustomTrackLibrary_PageLabel(const struct CustomTrackLibrary *library, size_t page,
                                 char *output, size_t outputSize);
/* Presentation-only UTF-8 paging. Returns the number of pages (at least one),
   wraps page, and copies only complete codepoints that fit output. Input must
   be NUL-terminated. Malformed bytes display as '?' without granting validity. */
size_t CustomTrackLibrary_TextPage(const char *text, size_t columns, size_t page,
                                 char *output, size_t outputSize);

#ifdef __cplusplus
}
#endif
#endif
