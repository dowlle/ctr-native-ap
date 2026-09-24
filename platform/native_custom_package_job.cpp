#include <platform/native_custom_package.h>
#include <platform/native_custom_track_library.h>
#include <platform/native_custom_offline.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <future>
#include <string>
#include <vector>
#include <memory>

namespace {
struct InstallResult { int result = 0; char pin[65] = {}; char error[512] = {}; };
std::future<InstallResult> installJob;
struct StoreResult { int ok = 0; std::vector<CustomPackageStoreEntry> rows; char error[512] = {}; };
std::future<StoreResult> storeJob;
struct OfflineDeleter { void operator()(CustomOfflineRequest *p) const { CustomOffline_Free(&p); } };
struct PackageDeleter { void operator()(CustomPackageOwned *p) const { CustomPackage_Free(&p); } };
struct OfflineResult
{
    std::unique_ptr<CustomOfflineRequest, OfflineDeleter> request;
    char error[512] = {};
};
std::future<OfflineResult> offlineJob;
void collect_store(const CustomPackageStoreEntry *entry, void *context)
{
    static_cast<StoreResult *>(context)->rows.push_back(*entry);
}
}

extern "C" int CustomPackage_StartInboxInstall(const char *assets, const char *pin)
{
    if (installJob.valid() || storeJob.valid() || offlineJob.valid() || !assets || !pin || std::strlen(assets) > 31000 ||
        std::strlen(pin) != 64 || std::strspn(pin, "0123456789abcdef") != 64) return 0;
    try
    {
        std::string path(assets), revision(pin);
        installJob = std::async(std::launch::async, [path, revision] {
            InstallResult result;
            std::memcpy(result.pin, revision.c_str(), sizeof result.pin);
            result.result = CustomPackage_InstallInbox(path.c_str(), revision.c_str(), result.error, sizeof result.error);
            return result;
        });
        return 1;
    }
    catch (const std::exception &) { return 0; }
}

extern "C" int CustomPackage_StartStoreScan(const char *store)
{
    if (!store || std::strlen(store) > 32000 || installJob.valid() || storeJob.valid() || offlineJob.valid()) return 0;
    try
    {
        std::string path(store);
        storeJob = std::async(std::launch::async, [path] {
            StoreResult result;
            result.ok = CustomPackage_ScanStore(path.c_str(), collect_store, &result, result.error, sizeof result.error);
            return result;
        });
        return 1;
    }
    catch (const std::exception &) { return 0; }
}

extern "C" int CustomPackage_PollStoreScan(CustomTrackLibrary *library, char *error, size_t errorSize)
{
    if (!storeJob.valid()) return -1;
    if (!library || !error || !errorSize) return 0;
    if (storeJob.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return 0;
    try
    {
        auto result = storeJob.get();
        if (!result.ok)
        {
            std::snprintf(error, errorSize, "%s", result.error);
            return -2;
        }
        if (!CustomTrackLibrary_ApplyStore(library, result.rows.data(), result.rows.size()))
        {
            std::snprintf(error, errorSize, "Cannot merge package store within library limits");
            return -2;
        }
        error[0] = 0;
        return 1;
    }
    catch (const std::exception &exception)
    {
        std::snprintf(error, errorSize, "%s", exception.what());
        return -2;
    }
}

extern "C" int CustomPackage_PollInboxInstall(int *result, char pin[65], char *error, size_t errorSize)
{
    if (!installJob.valid()) return -1;
    if (!result || !pin || !error || !errorSize) return 0; // Do not consume an unreportable result.
    if (installJob.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return 0;
    try
    {
        auto completed = installJob.get();
        *result = completed.result;
        std::memcpy(pin, completed.pin, sizeof completed.pin);
        std::snprintf(error, errorSize, "%s", completed.error);
    }
    catch (const std::exception &exception)
    {
        *result = 0; pin[0] = 0;
        std::snprintf(error, errorSize, "%s", exception.what());
    }
    return 1;
}

extern "C" int CustomOffline_StartPrepare(const char *assets, const char *pin)
{
    if (installJob.valid() || storeJob.valid() || offlineJob.valid() || !assets || !pin ||
        std::strlen(assets) > 31000 || std::strlen(pin) != 64 ||
        std::strspn(pin, "0123456789abcdef") != 64) return 0;
    try
    {
        const std::string path = std::string(assets) + "/tracks/packages/" + pin;
        const std::string revision(pin);
        offlineJob = std::async(std::launch::async, [path, revision] {
            OfflineResult result;
            CustomPackageOwned *raw = nullptr;
            if (!CustomPackage_AcquireDirectory(path.c_str(), revision.c_str(), &raw,
                result.error, sizeof result.error)) return result;
            std::unique_ptr<CustomPackageOwned, PackageDeleter> package(raw);
            unsigned int authoredLaps;
            if (!CustomPackage_GetRaceLaps(raw, &authoredLaps, result.error, sizeof result.error)) return result;
            if (authoredLaps > CTR_OFFLINE_MAX_LAPS)
            {
                std::snprintf(result.error, sizeof result.error, "Authored laps exceed the engine's seven-lap timing storage");
                return result;
            }
            CustomOfflineRequest *request = nullptr;
            if (!CustomOffline_Prepare(&raw, revision.c_str(), &request, result.error, sizeof result.error))
                return result;
            package.release(); // The request now owns that reference.
            result.request.reset(request);
            if (!CustomOffline_CheckStructure(request, result.error, sizeof result.error)) result.request.reset();
            return result;
        });
        return 1;
    }
    catch (const std::exception &) { return 0; }
}

extern "C" int CustomOffline_PollPrepare(CustomOfflineRequest **out, char *error, size_t errorSize)
{
    if (!offlineJob.valid()) return -1;
    if (!out || *out || !error || !errorSize) return 0;
    if (offlineJob.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return 0;
    try
    {
        auto completed = offlineJob.get();
        std::snprintf(error, errorSize, "%s", completed.error);
        if (!completed.request) return -2;
        *out = completed.request.release();
        return 1;
    }
    catch (const std::exception &exception)
    {
        std::snprintf(error, errorSize, "%s", exception.what());
        return -2;
    }
}
