/* Production profile/preflight probe for an isolated asset fixture.
 * cc -Wall -Wextra -Werror -DCTR_CUSTOM_TRACKS -Iinclude -I.
 *   tools/probe-custom-package-profile.c -o /tmp/ctr-profile-probe
 * Finalization is explicit and writes only the supplied fixture root.
 */
#include <stdio.h>
#include <string.h>
#include "platform/native_custom_track_manager.c"

int main(int argc, char **argv)
{
    const struct CustomTrackManagerPackage *package;
    struct CustomTrackManagerRequirement requirement;
    struct CustomTrackManagerStatus status;
    int state;
    if (argc != 3 && argc != 4) return 2;
    if (argc == 4 && strcmp(argv[3], "--finalize") != 0) return 2;
    if (strcmp(argv[2], "current") == 0) package = &s_babyTParkCurrent;
    else if (strcmp(argv[2], "legacy") == 0) package = CustomTrackManager_BabyTPark();
    else return 2;
    requirement = (struct CustomTrackManagerRequirement) {
        package->id, package->packageUuid, package->version,
        package->minimumClientVersion, package->minimumApworldVersion,
        package->levSha256, package->vrmSha256, package->navigationUuid,
        package->navigationRevision, package->laps, 0,
        package->flagCrates, package->flagCtrLetters, package->flagRelicCrates,
        package->flagAiNav, package->flagMinimap, package->flagGhosts,
        package->flagWumpaCollectible, package->flagSpawns, package->flagCheckpoints
    };
    state = CustomTrackManager_Preflight(argv[1], &requirement, argc == 4, &status);
    printf("profile=%s state=%d root=%s wumpa=%d detail=%s\n",
           package->version, state, status.packageRoot,
           status.wumpa.guaranteedFruit, status.detail);
    return state == CTR_CT_MANAGER_READY ? 0 : 1;
}
