#ifndef AP_HOOKS_H
#define AP_HOOKS_H

// authoring-client branch: the Archipelago layer is not on this branch. The
// authoring modules include this header for the few AP-layer services they
// call, which the authoring host provides (ap_authoring_host.h). On main this
// file is the full Archipelago hook header. CMakeLists.txt refuses CTR_AP here.

#include "ap_authoring_host.h"

#endif // AP_HOOKS_H
