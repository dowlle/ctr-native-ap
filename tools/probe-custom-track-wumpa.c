/* Read-only production asset measurement. Does not install/finalize packages.
 * cc -Wall -Wextra -Werror -DCTR_CUSTOM_TRACKS -Iinclude -I.
 *   tools/probe-custom-track-wumpa.c -o /tmp/ctr-custom-wumpa-probe
 */
#include <stdio.h>
#include "platform/native_custom_track_manager.c"

int main(int argc, char **argv)
{
    struct CustomTrackWumpaMeasurement measurement;
    if (argc != 2) {
        fprintf(stderr, "usage: %s track.lev\n", argv[0]);
        return 2;
    }
    if (!CustomTrackManager_MeasureWumpa(argv[1], &measurement)) {
        fprintf(stderr, "FAIL: production measurement rejected asset\n");
        return 1;
    }
    printf("fruit_crates=%d loose_fruit=%d guaranteed_fruit=%d collectible=%d\n",
           measurement.fruitCrates, measurement.looseFruit,
           measurement.guaranteedFruit, measurement.collectible);
    return 0;
}
