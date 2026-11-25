#ifndef CTR_NATIVE_CORTEX_ASSET_PINS_H
#define CTR_NATIVE_CORTEX_ASSET_PINS_H

/* The Lockheart Cortex Vortex geometry bundled with the client, a merged mode
   build like a package with `relic_files` (CV1). The main pair is Saphi 1.1.5
   (crates, CTR letters, Arcade grid) and serves every mode except the Relic
   Race; the relic pair is Saphi 1.1.6 (the 100 time crates) and serves only a
   Relic Race. Both share the road, and every object they share sits at the
   same place in the same instance order as the former all-in-one 1.1.2 build
   (LEV 4e3a2daf...), so its crate rulings apply unchanged. */
#define CTR_CFG_CORTEX_LEV_SHA256 "cabeba054ad1416a67ab2af2dcaad46e8ded71213319035750377b244fd9aa1a"
#define CTR_CFG_CORTEX_VRM_SHA256 "a81cb4aa483d884b4d6b616bb12e0e8778439c1f4075d79b3807682a19dbe914"
#define CTR_CFG_CORTEX_RELIC_LEV_SHA256 "08afeda329194b737014f29324d0930bafbd0b940d02e72b29b37b0e9cded911"
#define CTR_CFG_CORTEX_RELIC_VRM_SHA256 "9e15a816e547b8a3b3d913a1767d58b1866e9b9d643f11cd3411381da6255500"
/* The all-in-one build bundled before CV1. Rooms that recorded it in their
   sanity rows still admit: its crates are 1.1.5's, same places, same order. */
#define CTR_CFG_CORTEX_LEGACY_LEV_SHA256 "4e3a2daf56c67be3ac645d3bb5375e516c828a0bca24c35ac69b3366c466fe13"

#define CTR_CFG_CORTEX_LEV_FILE "CVortex Arcade All.lev"
#define CTR_CFG_CORTEX_VRM_FILE "CVortex Arcade All.vrm"
#define CTR_CFG_CORTEX_RELIC_LEV_FILE "CVortex Relic.lev"
#define CTR_CFG_CORTEX_RELIC_VRM_FILE "CVortex Relic.vrm"

#endif
