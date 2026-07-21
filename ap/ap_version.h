#ifndef AP_VERSION_H
#define AP_VERSION_H

// CTR-AP release identifier -- the version of the SHIPPED PAIR (this client +
// its companion ctr.apworld), stamped at release time so a bug report can name
// the exact release it came from (release-management plan, proposal 1). This is
// deliberately separate from CTR_NATIVE_VERSION (the underlying decomp engine
// version, e.g. 0.1.0-beta.6.1): the engine and the AP release move on
// different trains. Surfaced in the window title and the startup log; update
// this define as part of tagging a release.
#define CTR_AP_VERSION "v0.1.3"

#endif // AP_VERSION_H
