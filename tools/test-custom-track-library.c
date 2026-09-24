// cc -std=c11 -Wall -Wextra -Werror -I include tools/test-custom-track-library.c
// platform/native_custom_track_library.c -o /tmp/test-custom-track-library
#include <platform/native_custom_track_library.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

static int checks, failures;
#define CHECK(condition) do { checks++; if (!(condition)) { failures++; printf("FAIL line %d: %s\n", __LINE__, #condition); } } while (0)

static struct CustomTrackLibraryEntry entry(int index, const char *title)
{
    struct CustomTrackLibraryEntry result = {0};
    snprintf(result.manifestSha256, sizeof result.manifestSha256, "%064x", index);
    snprintf(result.title, sizeof result.title, "%s", title);
    snprintf(result.version, sizeof result.version, "1.0.%d", index);
    snprintf(result.author, sizeof result.author, "Creator");
    result.supportedModes = 1;
    return result;
}

static size_t append_text(unsigned char *buffer, size_t offset, const char *text)
{
    size_t length = strlen(text);
    buffer[offset] = (unsigned char)length;
    buffer[offset+1] = (unsigned char)(length >> 8);
    buffer[offset+2] = buffer[offset+3] = 0;
    memcpy(buffer + offset + 4, text, length);
    return offset + 4 + length;
}

static void test_text_pages(void)
{
    char output[641], title[641], rebuilt[641] = {0};
    size_t i;
    CHECK(CustomTrackLibrary_TextPage("", 40, 0, output, sizeof output) == 1);
    CHECK(!output[0]);
    CHECK(CustomTrackLibrary_TextPage(NULL, 40, 0, output, sizeof output) == 0);
    CHECK(CustomTrackLibrary_TextPage("abc", 0, 0, output, sizeof output) == 0);
    CHECK(CustomTrackLibrary_TextPage("abcdef", 2, 4, output, sizeof output) == 3);
    CHECK(!strcmp(output, "cd"));
    CHECK(CustomTrackLibrary_TextPage("Caf\xC3\xA9", 3, 1, output, sizeof output) == 2);
    CHECK(!strcmp(output, "\xC3\xA9"));
    CHECK(CustomTrackLibrary_TextPage("\xF0\x9F\x8F\x81", 1, 0, output, 4) == 1);
    CHECK(!output[0]); /* No partial four-byte character. */
    CHECK(CustomTrackLibrary_TextPage("\xF0\x9F\x8F\x81", 1, 0, output, 5) == 1);
    CHECK(!strcmp(output, "\xF0\x9F\x8F\x81"));
    for (i = 0; i < 160; i++) memcpy(title + i * 4, "\xF0\x9F\x8F\x81", 4);
    title[640] = 0;
    for (i = 0; i < 4; i++)
    {
        CHECK(CustomTrackLibrary_TextPage(title, 40, i, output, sizeof output) == 4);
        CHECK(strlen(output) == 160);
        strcat(rebuilt, output);
    }
    CHECK(!strcmp(rebuilt, title));
    CHECK(CustomTrackLibrary_TextPage("\xC0\xAF\xED\xA0\x80\xF4\x90\x80\x80", 40, 0, output, sizeof output) == 1);
    CHECK(!strcmp(output, "?????????")); /* Overlong, surrogate, out-of-range. */
    CHECK(CustomTrackLibrary_TextPage("\xF0\x9F", 40, 0, output, sizeof output) == 1);
    CHECK(!strcmp(output, "??")); /* Truncated input never reads past NUL. */
    output[0] = 'x';
    CHECK(CustomTrackLibrary_TextPage("abc", 2, (size_t)-1, output, 0) == 2);
    CHECK(output[0] == 'x');
    CHECK(CustomTrackLibrary_TextPage("abc", 2, 0, NULL, 0) == 2);
}

static void test_catalogue(void)
{
    struct CustomTrackLibrary library = {0};
    unsigned char bytes[512] = "CTRLB001";
    size_t size = 12, i;
    char hash[65];
    memset(hash, 'a', 64); hash[64] = 0;
    bytes[8] = 1;
    size = append_text(bytes, size, hash);
    size = append_text(bytes, size, "Track name");
    size = append_text(bytes, size, "1.0.0");
    size = append_text(bytes, size, "Author");
    CHECK(CustomTrackLibrary_ParseCatalogue(&library, bytes, size));
    CHECK(library.count == 1);
    CHECK(!strcmp(CustomTrackLibrary_Selected(&library)->title, "Track name"));
    CHECK(!CustomTrackLibrary_Selected(&library)->installed);
    CHECK(!CustomTrackLibrary_Selected(&library)->supportedModes);
    CHECK(!CustomTrackLibrary_Selected(&library)->requiredBySeed);
    CHECK(CustomTrackLibrary_Selected(&library)->disabledReason[0]);
    for (i = 0; i < size; i++) CHECK(!CustomTrackLibrary_ParseCatalogue(&library, bytes, i));
    CHECK(library.count == 1);
    CHECK(!CustomTrackLibrary_ParseCatalogue(&library, bytes, size + 1));
    bytes[12] = 255;
    CHECK(!CustomTrackLibrary_ParseCatalogue(&library, bytes, size));
    CustomTrackLibrary_Free(&library);
}

static void test_arcade_pages(void)
{
    struct CustomTrackLibrary library = {0};
    struct CustomTrackLibraryEntry *items = calloc(101, sizeof *items);
    char label[16], title[32];
    size_t count = 0;
    CHECK(CustomTrackLibrary_PageCount(&library) == 1);
    CHECK(CustomTrackLibrary_PageRowCount(&library, 0) == 0);
    CustomTrackLibrary_PageLabel(&library, 0, label, sizeof label);
    CHECK(!strcmp(label, "Custom"));
    CHECK(items != NULL);
    if (!items) return;
    for (int i = 0; i < 101; i++)
    {
        snprintf(title, sizeof title, "%c Track %03d", 'A' + i % 26, i);
        items[i] = entry(i + 1, title);
    }
    strcpy(items[0].title, "9 Track");
    strcpy(items[1].title, "[Track]");
    CHECK(CustomTrackLibrary_Replace(&library, items, 101));
    CHECK(CustomTrackLibrary_PageCount(&library) == 13);
    CustomTrackLibrary_PageLabel(&library, 0, label, sizeof label);
    CHECK(!strcmp(label, "#-B"));
    for (size_t p = 0; p < 13; p++)
    {
        size_t rows = CustomTrackLibrary_PageRowCount(&library, p);
        CHECK(rows == (p == 12 ? 5 : 8));
        for (size_t r = 0; r < rows; r++)
            CHECK(CustomTrackLibrary_Row(&library, p * CTR_LIBRARY_PAGE_ROWS + r) != NULL);
        count += rows;
    }
    CHECK(count == 101);
    CHECK(CustomTrackLibrary_PageRowCount(&library, (size_t)-1) == 0);
    CHECK(CustomTrackLibrary_PageRowCount(&library, 13) == 0);
    CHECK(CustomTrackLibrary_Filter(&library, "Z Track", 0, 0));
    CHECK(CustomTrackLibrary_PageCount(&library) == 1);
    CHECK(CustomTrackLibrary_PageRowCount(&library, 0) == 3);
    CustomTrackLibrary_PageLabel(&library, 0, label, sizeof label);
    CHECK(!strcmp(label, "Z"));
    CustomTrackLibrary_Free(&library);
    free(items);
}

int main(int argc, char **argv)
{
    struct CustomTrackLibrary library = {0};
    struct CustomTrackLibraryEntry rows[3] = {entry(1, "Zebra"), entry(2, "Alpha"), entry(3, "Alpha")};
    char pin[65];
    int i;
    if (argc == 2)
    {
        CHECK(CustomTrackLibrary_ReadCatalogue(&library, argv[1]));
        printf("catalogue rows: %lu\n", (unsigned long)library.count);
        for (i = 0; i < (int)library.count; i++)
            printf("%s | %s\n", library.entries[i].title, library.entries[i].manifestSha256);
        CustomTrackLibrary_Free(&library);
        return failures != 0;
    }
    test_catalogue();
    test_text_pages();
    test_arcade_pages();
    CHECK(CustomTrackLibrary_Selected(&library) == NULL);
    CustomTrackLibrary_Move(&library, INT_MIN);
    rows[0].requiredBySeed = 1;
    strcpy(rows[0].disabledReason, "Pinned files missing");
    CHECK(CustomTrackLibrary_Replace(&library, rows, 3));
    CHECK(library.visibleCount == 3);
    CHECK(!strcmp(CustomTrackLibrary_Selected(&library)->title, "Alpha"));
    CHECK(CustomTrackLibrary_Row(&library, 3) == NULL);
    CustomTrackLibrary_Move(&library, -1);
    CHECK(!strcmp(CustomTrackLibrary_Selected(&library)->title, "Zebra"));
    CHECK(!strcmp(CustomTrackLibrary_Selected(&library)->disabledReason, "Pinned files missing"));
    strcpy(pin, CustomTrackLibrary_Selected(&library)->manifestSha256);
    CHECK(CustomTrackLibrary_Filter(&library, "ALPHA", 0, 0));
    CHECK(library.visibleCount == 2);
    CHECK(CustomTrackLibrary_Filter(&library, "no matches", 0, 0));
    CHECK(CustomTrackLibrary_Selected(&library) == NULL);
    CHECK(CustomTrackLibrary_Filter(&library, "", 0, 0));
    CHECK(!strcmp(CustomTrackLibrary_Selected(&library)->manifestSha256, pin));
    CHECK(CustomTrackLibrary_Filter(&library, "creator", 0, 1));
    CHECK(library.visibleCount == 1);
    CustomTrackLibrary_Move(&library, 1);
    CHECK(library.selected == 0);
    CHECK(CustomTrackLibrary_Filter(&library, "", 2, 0));
    CHECK(library.visibleCount == 0);
    CHECK(CustomTrackLibrary_Filter(&library, "", 0, 0));
    strcpy(rows[1].title, "Changed caller buffer");
    CHECK(strcmp(CustomTrackLibrary_Row(&library, 0)->title, rows[1].title));
    rows[1] = rows[0];
    CHECK(!CustomTrackLibrary_Replace(&library, rows, 3));
    CHECK(library.count == 3);
    CHECK(!CustomTrackLibrary_Replace(&library, NULL, CTR_LIBRARY_MAX + 1));
    CHECK(!CustomTrackLibrary_Replace(&library, NULL, 1));
    {
        struct CustomTrackLibraryEntry *large = calloc(132, sizeof *large);
        if (!large) return 2;
        for (i = 0; i < 132; i++) large[i] = entry(i + 1, "Same title, different revision");
        CHECK(CustomTrackLibrary_Replace(&library, large, 132));
        CHECK(library.visibleCount == 132);
        for (i = 0; i < 132; i++) CustomTrackLibrary_Move(&library, 1);
        CHECK(!strcmp(CustomTrackLibrary_Selected(&library)->manifestSha256, pin));
        free(large);
    }
    CHECK(CustomTrackLibrary_Replace(&library, NULL, 0));
    CHECK(CustomTrackLibrary_Selected(&library) == NULL);
    for (i = 0; i < 3; i++) rows[i] = entry(i + 1, "Install projection fixture");
    CHECK(CustomTrackLibrary_Replace(&library, rows, 3));
    CHECK(CustomTrackLibrary_MarkInstalled(&library, rows[0].manifestSha256));
    for (i = 0; i < 3; i++)
    {
        const struct CustomTrackLibraryEntry *item = &library.entries[i];
        if (!strcmp(item->manifestSha256, rows[0].manifestSha256))
        {
            CHECK(item->installed == 1);
            CHECK(item->supportedModes == 0);
            CHECK(strstr(item->disabledReason, "certification") != NULL);
        }
        else CHECK(item->installed == 0);
    }
    CHECK(!CustomTrackLibrary_MarkInstalled(&library, "unknown"));
    {
        struct CustomPackageStoreEntry store[2] = {0};
        struct CustomTrackLibraryEntry newEntry = entry(9, "Installed only");
        strcpy(store[0].pin, rows[0].manifestSha256);
        strcpy(store[0].manifest.sha256, store[0].pin);
        strcpy(store[0].manifest.title, "Verified title replaces catalogue title");
        strcpy(store[0].manifest.version, "2.0.0");
        store[0].verified = 1;
        strcpy(store[1].pin, newEntry.manifestSha256);
        strcpy(store[1].error, "Damaged revision");
        CHECK(CustomTrackLibrary_ApplyStore(&library, store, 2));
        CHECK(library.count == 4);
        for (i = 0; i < (int)library.count; i++)
        {
            const struct CustomTrackLibraryEntry *item = &library.entries[i];
            CHECK(item->supportedModes == 0);
            if (!strcmp(item->manifestSha256, store[0].pin))
            {
                CHECK(item->installed);
                CHECK(!strcmp(item->title, store[0].manifest.title));
                CHECK(!strcmp(item->version, "2.0.0"));
            }
            if (!strcmp(item->manifestSha256, store[1].pin))
            {
                CHECK(!item->installed);
                CHECK(!strcmp(item->disabledReason, "Damaged revision"));
            }
        }
        store[1] = store[0];
        CHECK(!CustomTrackLibrary_ApplyStore(&library, store, 2));
        CHECK(library.count == 4);
        CHECK(CustomTrackLibrary_ApplyStore(&library, NULL, 0));
        for (i = 0; i < (int)library.count; i++) CHECK(!library.entries[i].installed);
    }
    CustomTrackLibrary_Free(&library);
    CustomTrackLibrary_Free(&library);
    printf("custom track library: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
