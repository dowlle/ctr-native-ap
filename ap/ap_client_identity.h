#ifndef AP_CLIENT_IDENTITY_H
#define AP_CLIENT_IDENTITY_H

#include <string>

#ifdef CTR_AP
#include "ap_version.h" // CTR_AP_VERSION, the version in the title and the boot log
#endif

// The User-Agent on every request the download transport sends: the client's
// real name and shipped version plus the public repository, in the usual
// Name/version (+url) form. No host, player or machine data.
inline std::string ap_client_user_agent()
{
#ifdef CTR_AP
	return std::string("CTR-AP/") + CTR_AP_VERSION + " (+https://github.com/dowlle/ctr-native-ap)";
#else
	// The authoring client carries no AP version in its title or log; it
	// identifies as the engine build it is (main.c's "[CTR Native] Version").
	return std::string("CTR-Native-Authoring/") + CTR_NATIVE_VERSION + " (+https://github.com/dowlle/ctr-native-ap)";
#endif
}

#endif // AP_CLIENT_IDENTITY_H
