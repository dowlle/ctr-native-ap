#ifdef CTR_CUSTOM_TRACKS
#include <platform/native_custom_package.h>
#include <platform/native_custom_content_verify.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <limits.h>

struct CustomPackageOwned
{
    atomic_uint references;
    struct CustomPackageManifest manifest;
    unsigned char *bytes[CTR_PACKAGE_FILE_MAX];
    struct CustomContentVerification pairReport;
};

static void package_error(char *error, size_t size, const char *reason)
{
    if (error && size) snprintf(error, size, "%s", reason);
}

void CustomPackage_Free(struct CustomPackageOwned **package)
{
    struct CustomPackageOwned *released;
    size_t i;
    if (!package || !*package) return;
    released = *package;
    *package = NULL;
    if (atomic_fetch_sub_explicit(&released->references, 1, memory_order_acq_rel) != 1) return;
    for (i = 0; i < CTR_PACKAGE_FILE_MAX; i++) free(released->bytes[i]);
    free(released);
}

int CustomPackage_Retain(struct CustomPackageOwned *package, struct CustomPackageOwned **out)
{
    unsigned int references;
    if (!package || !out || *out) return 0;
    references = atomic_load_explicit(&package->references, memory_order_relaxed);
    do
    {
        if (!references || references == UINT_MAX) return 0;
    } while (!atomic_compare_exchange_weak_explicit(&package->references, &references,
        references + 1, memory_order_relaxed, memory_order_relaxed));
    *out = package;
    return 1;
}

size_t CustomPackage_SnapshotBytes(const struct CustomPackageOwned *package)
{
    size_t total = sizeof *package;
    unsigned int i;
    if (!package) return 0;
    for (i = 0; i < package->manifest.count; i++) total += package->manifest.files[i].bytes;
    return total;
}

int CustomPackage_AcquireBuffers(const char *json, size_t size, const char *expected,
    const struct CustomPackageInput *inputs, size_t count, struct CustomPackageOwned **out,
    char *error, size_t errorSize)
{
    struct CustomPackageOwned *candidate;
    struct CustomContentOwnedPair pair = {0}; /* Borrows candidate buffers only during validation. */
    size_t i, j;
    unsigned char used[CTR_PACKAGE_FILE_MAX] = {0};
    package_error(error, errorSize, "");
    if (!out || *out || !inputs || count < 2 || count > CTR_PACKAGE_FILE_MAX)
    {
        package_error(error, errorSize, "Expected complete input list and empty package output");
        return 0;
    }
    candidate = calloc(1, sizeof *candidate);
    if (!candidate)
    {
        package_error(error, errorSize, "Package allocation failed");
        return 0;
    }
    atomic_init(&candidate->references, 1);
    if (!CustomPackage_ParseManifest(json, size, expected, &candidate->manifest, error, errorSize)) goto fail;
    if (count != candidate->manifest.count)
    {
        package_error(error, errorSize, "Input count does not match complete package");
        goto fail;
    }
    for (i = 0; i < count; i++)
    {
        const struct CustomPackageFile *file = &candidate->manifest.files[i];
        struct NativeSha256Ctx sha;
        unsigned char digest[32];
        char actual[65];
        for (j = 0; j < count; j++)
            if (!used[j] && inputs[j].role && !strcmp(file->role, inputs[j].role)) break;
        if (j == count || !inputs[j].data || inputs[j].size != file->bytes)
        {
            package_error(error, errorSize, "Missing, duplicate or incorrectly sized package input");
            goto fail;
        }
        used[j] = 1;
        candidate->bytes[i] = malloc(file->bytes);
        if (!candidate->bytes[i])
        {
            package_error(error, errorSize, "Package file allocation failed");
            goto fail;
        }
        memcpy(candidate->bytes[i], inputs[j].data, file->bytes);
        NativeSha256_Init(&sha);
        NativeSha256_Update(&sha, candidate->bytes[i], file->bytes);
        NativeSha256_Final(&sha, digest);
        NativeSha256_ToHex(digest, actual);
        if (!NativeSha256_HexEquals(file->sha256, actual))
        {
            if (error && errorSize) snprintf(error, errorSize, "Package %s bytes do not match pin", file->role);
            goto fail;
        }
        if (!strcmp(file->role, "lev"))
        {
            pair.lev = candidate->bytes[i]; pair.levBytes = file->bytes;
            memcpy(pair.report.levSha256, actual, sizeof actual);
        }
        if (!strcmp(file->role, "vrm"))
        {
            pair.vrm = candidate->bytes[i]; pair.vrmBytes = file->bytes;
            memcpy(pair.report.vrmSha256, actual, sizeof actual);
        }
    }
    if (!CustomContentVerify_ValidateLoadablePair(&pair, error, errorSize)) goto fail;
    candidate->pairReport = pair.report;
    *out = candidate;
    return 1;
fail:
    CustomPackage_Free(&candidate);
    return 0;
}

int CustomPackage_GetManifest(const struct CustomPackageOwned *package, struct CustomPackageManifest *out)
{
    if (!package || !out) return 0;
    *out = package->manifest;
    return 1;
}

int CustomPackage_GetPairReport(const struct CustomPackageOwned *package, struct CustomContentVerification *out)
{
    if (!package || !out) return 0;
    *out = package->pairReport;
    return 1;
}

int CustomPackage_CopyFile(const struct CustomPackageOwned *package, const char *role,
                          void *destination, size_t capacity, size_t *written)
{
    size_t i;
    if (written) *written = 0;
    if (!package || !role || !destination) return 0;
    for (i = 0; i < package->manifest.count; i++)
        if (!strcmp(role, package->manifest.files[i].role))
        {
            size_t size = package->manifest.files[i].bytes;
            if (capacity < size) return 0;
            memcpy(destination, package->bytes[i], size);
            if (written) *written = size;
            return 1;
        }
    return 0;
}
#endif
