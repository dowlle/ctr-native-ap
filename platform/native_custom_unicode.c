#include <platform/native_custom_package.h>
#include <utf8proc.h>
#include <stdlib.h>
#include <string.h>

/* Python str.strip uses Zs or bidirectional WS/B/S. Other category-C
   characters are refused independently, including controls and unassigned. */
static int package_space(const utf8proc_property_t *property)
{
    return property->category == UTF8PROC_CATEGORY_ZS ||
        property->bidi_class == UTF8PROC_BIDI_CLASS_WS ||
        property->bidi_class == UTF8PROC_BIDI_CLASS_B ||
        property->bidi_class == UTF8PROC_BIDI_CLASS_S;
}

int CustomPackage_ValidateTitle(const char *input, size_t size)
{
    size_t offset = 0, count = 0;
    utf8proc_uint8_t *normalized = NULL;
    utf8proc_ssize_t normalizedSize;
    int valid;
    if (!input || !size || size > 640) return 0;
    while (offset < size)
    {
        utf8proc_int32_t codepoint;
        const utf8proc_property_t *property;
        utf8proc_ssize_t length = utf8proc_iterate((const utf8proc_uint8_t *)input + offset,
                                                 (utf8proc_ssize_t)(size - offset), &codepoint);
        if (length <= 0 || ++count > 160) return 0;
        property = utf8proc_get_property(codepoint);
        if (property->category == UTF8PROC_CATEGORY_CN || property->category >= UTF8PROC_CATEGORY_CC)
            return 0;
        if ((offset == 0 || offset + (size_t)length == size) && package_space(property)) return 0;
        offset += (size_t)length;
    }
    normalizedSize = utf8proc_map((const utf8proc_uint8_t *)input, (utf8proc_ssize_t)size,
                                  &normalized, UTF8PROC_STABLE | UTF8PROC_COMPOSE);
    valid = normalizedSize >= 0 && (size_t)normalizedSize == size && !memcmp(input, normalized, size);
    free(normalized);
    return valid;
}
