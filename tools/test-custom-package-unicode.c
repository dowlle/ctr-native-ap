// cc -m32 -std=c11 -Wall -Wextra -Werror -DUTF8PROC_STATIC -Iinclude -Ivendor/utf8proc
// tools/test-custom-package-unicode.c platform/native_custom_unicode.c vendor/utf8proc/utf8proc.c
// -o /tmp/test-custom-package-unicode
#include <platform/native_custom_package.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    const char *valid[] = {"Track", "Caf\xc3\xa9", "\xed\x95\x9c\xea\xb8\x80", "A B"};
    const char *invalid[] = {"", "Cafe\xcc\x81", "\xc2\xa0Track", "Track\xe2\x80\x8b",
        "\xee\x80\x80", "\xcd\xb8", "\xc0\xaf", "\xed\xa0\x80", "\xe2\x82",
        "\xe1\x84\x80\xe1\x85\xa1", "Track ", " Track"};
    unsigned int i, checks = 0, failures = 0;
    char longTitle[162];
#define CHECK(value) do { checks++; if (!(value)) { failures++; printf("FAIL line %d\n", __LINE__); } } while (0)
    for (i = 0; i < sizeof(valid) / sizeof(valid[0]); i++)
        CHECK(CustomPackage_ValidateTitle(valid[i], strlen(valid[i])));
    for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++)
        CHECK(!CustomPackage_ValidateTitle(invalid[i], strlen(invalid[i])));
    CHECK(!CustomPackage_ValidateTitle(NULL, 1));
    CHECK(!CustomPackage_ValidateTitle("a\0b", 3));
    memset(longTitle, 'x', sizeof longTitle);
    CHECK(CustomPackage_ValidateTitle(longTitle, 160));
    CHECK(!CustomPackage_ValidateTitle(longTitle, 161));
    printf("custom package Unicode: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
