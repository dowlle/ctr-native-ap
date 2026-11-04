// cc -m32 -ffunction-sections -fdata-sections -Wl,--gc-sections -DCTR_AP -DCTR_NATIVE -DCTR_CUSTOM_TRACKS -DCTR_CUSTOM_PACKAGES -DBUILD=926 -I include -I . tools/test-custom-selector-player.c
// platform/native_custom_track_library.c platform/native_custom_records.c -lm -o /tmp/test-custom-selector-player
/* The custom pages as the player client builds them: Single Race and Time
   Trial, and a room is no refusal. The scenarios are test-custom-selector.c's. */
#include "test-custom-selector.c"
