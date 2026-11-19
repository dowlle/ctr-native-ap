#include <platform/native_custom_offline.h>
#include <platform/native_custom_package.h>
#include <platform/native_custom_state_identity.h>
#include <platform/native_recorded_race_data.h>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

static std::vector<char> read(const std::string &path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}

int main(int argc, const char **argv)
{
    assert(argc == 3);
    const std::string root = argv[1];
    const auto manifest = read(root + "/manifest.json");
    CustomPackageManifest meta;
    char reason[256];
    if (!CustomPackage_ParseManifest(manifest.data(), manifest.size(), argv[2], &meta, reason, sizeof reason))
    {
        puts(reason);
        return 0;
    }
    std::vector<char> files[CTR_PACKAGE_FILE_MAX];
    CustomPackageInput inputs[CTR_PACKAGE_FILE_MAX];
    for (unsigned int i = 0; i < meta.count; i++)
    {
        files[i] = read(root + "/" + meta.files[i].path);
        inputs[i] = {meta.files[i].role, files[i].data(), files[i].size()};
    }
    CustomPackageOwned *package = nullptr;
    if (!CustomPackage_AcquireBuffers(manifest.data(), manifest.size(), argv[2],
            inputs, meta.count, &package, reason, sizeof reason))
    {
        puts(reason);
        return 0;
    }
    std::vector<char> lev;
    for (unsigned int i = 0; i < meta.count; i++)
    {
        if (!strcmp(meta.files[i].role, "lev")) lev = files[i];
        std::fill(files[i].begin(), files[i].end(), 0);
    }
    int arcade = CustomOffline_PackageSelectorArcadeReason(package, reason, sizeof reason);
    const std::string selectorReason = reason;
    const bool originalArcade = CustomOffline_PackageArcade(package);
    const bool timeTrial = CustomOffline_PackageTimeTrialReason(package, nullptr, 0);
    CustomOfflineRequest *request = nullptr;
    assert(CustomOffline_Prepare(&package, argv[2], &request, reason, sizeof reason));
    assert(!package);
    assert(CustomOffline_CheckStructure(request, CTR_OFFLINE_MODE_TIME_TRIAL, reason, sizeof reason));
    assert(arcade == CustomOffline_CheckStructure(request, CTR_OFFLINE_MODE_ARCADE, reason, sizeof reason));
    assert(selectorReason == reason);
    assert(arcade == CustomOffline_BeginRuntime(&request, 6, 0, CTR_OFFLINE_MODE_ARCADE));
    if (!arcade)
    {
        assert(timeTrial == bool(CustomOffline_BeginRuntime(&request, 6, 0, CTR_OFFLINE_MODE_TIME_TRIAL)));
        assert(!CustomOffline_RuntimeRecordedRace(6, CTR_OFFLINE_MODE_TIME_TRIAL));
        CustomOffline_Free(&request);
        puts(selectorReason.c_str());
    }
    else if (originalArcade)
    {
        assert(!CustomOffline_RuntimeRecordedRace(6, CTR_OFFLINE_MODE_ARCADE));
        puts("Ordinary Arcade admitted");
    }
    else
    {
        const auto *race = CustomOffline_RuntimeRecordedRace(6, CTR_OFFLINE_MODE_ARCADE);
        assert(race && race->set.header.lapCount == 7 && race->crateCount >= 16);
        std::vector<char> served(lev.size());
        assert(CustomOffline_ReadRuntimeFile(2, served.data(), served.size(), served.size()));
        assert(served == lev);
        assert(!CustomOffline_RuntimeRecordedRace(6, CTR_OFFLINE_MODE_TIME_TRIAL));
        assert(!CustomOffline_RuntimeRecordedRace(0, CTR_OFFLINE_MODE_ARCADE));
        CustomStateIdentity identity, changed;
        CustomOffline_RuntimeStateIdentity(&identity);
        changed = identity;
        changed.recordedRacePin[0] ^= 1;
        assert(!strcmp(identity.recordedRacePin, argv[2]));
        assert(CustomStateIdentity_RestoreAllowed(&identity, &identity));
        assert(!CustomStateIdentity_RestoreAllowed(&identity, &changed));
        /* Independently exercise a malformed cycle even though the wrong hash
           prevents this geometry from getting as far as placement admission. */
        auto *raw = reinterpret_cast<unsigned char *>(lev.data());
        auto checkpoints = 4 + AP_NavRecFormat_GetU32(raw + 4 + 0x14c);
        auto next = raw[checkpoints + 8];
        raw[checkpoints + 8] = 0;
        assert(!strcmp(CustomRecordedRace_CheckGeometry(raw, lev.size(), race->grid,
            race->crates, race->crateCount), "Invalid lap cycle"));
        raw[checkpoints + 8] = next;
        memset(raw + 4, 0, 4);
        assert(!strcmp(CustomRecordedRace_CheckGeometry(raw, lev.size(), race->grid,
            race->crates, race->crateCount), "Invalid ground mesh"));
        puts("Recorded Arcade admitted");
    }
    CustomOffline_EndRuntime();
    assert(!CustomOffline_RuntimeRecordedRace(6, CTR_OFFLINE_MODE_ARCADE));
}
