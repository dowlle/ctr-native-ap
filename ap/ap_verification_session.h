#ifndef AP_VERIFICATION_SESSION_H
#define AP_VERIFICATION_SESSION_H

#ifdef __cplusplus
extern "C" {
#endif

#define AP_VERIFY_TEXT_MAX 512

struct ap_verification_session
{
	char nonce[65];
	char variant_id[65];
	char content_id[65];
	long long track_id;
	char version[AP_VERIFY_TEXT_MAX];
	long long lev_media_id;
	long long vrm_media_id;
	char lev_path[AP_VERIFY_TEXT_MAX];
	char vrm_path[AP_VERIFY_TEXT_MAX];
	char lev_sha256[65];
	char vrm_sha256[65];
	long long lev_bytes;
	long long vrm_bytes;
	int host;
	int mode;
	long long expires_unix;
};

int ap_verification_session_load(const char *manifest_path,
	                              struct ap_verification_session *out,
	                              char *error, int error_bytes);
int ap_verification_session_write_receipt(const char *receipt_path,
	                                       const struct ap_verification_session *session,
	                                       const char *git_commit, int dirty,
	                                       const char *feature_configuration,
	                                       char *error, int error_bytes);
int ap_verification_session_finalize_receipt(const char *receipt_path,
	                                          char *error, int error_bytes);
int ap_verification_session_consume_manifest(const char *pending_path,
	                                          const char *armed_path,
	                                          char *error, int error_bytes);

#ifdef __cplusplus
}
#endif
#endif
