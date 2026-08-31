// Offline fixed-manifest and machine-receipt harness.
// Build with the same nlohmann/OpenSSL includes used by ap_net.

#include "ap/ap_verification_session.h"
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include <unistd.h>

using json = nlohmann::json;
static int checks, failures;
static void expect(bool value, const char *name) { ++checks; if (!value) { ++failures; std::cout << "FAIL " << name << "\n"; } }
static void write_json(const std::filesystem::path &path, const json &value) { std::ofstream(path) << value.dump(2) << '\n'; }

int main()
{
	char scratch[] = "/tmp/ctr-verification-session-XXXXXX";
	if (!mkdtemp(scratch) || chdir(scratch) != 0) return 2;
	std::filesystem::create_directories("custom-content-lab/media/1");
	std::filesystem::create_directories("custom-content-lab/session");
	std::ofstream("custom-content-lab/media/1/lev-2.lev") << "lev";
	std::ofstream("custom-content-lab/media/1/vrm-3.vrm") << "vrm";
	const auto lev = std::filesystem::absolute("custom-content-lab/media/1/lev-2.lev").string();
	const auto vrm = std::filesystem::absolute("custom-content-lab/media/1/vrm-3.vrm").string();
	const long long now = static_cast<long long>(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
	json valid = {{"schema_version",2},{"nonce",std::string(64,'c')},
		{"variant_id","f17013ddb419ec392335bcfe02bba4a7f020a32511c0dd30f01a4d21db51e682"},
		{"content_id","1a6d13687768a8be00f173ba0052f3b72bb7f1420ff4c8523a887d5e658b388e"},
		{"track_id",1},{"version","1.0.0"},{"lev_media_id",2},{"vrm_media_id",3},
		{"lev_path",lev},{"vrm_path",vrm},{"lev_sha256",std::string(64,'a')},{"vrm_sha256",std::string(64,'b')},
		{"lev_bytes",3},{"vrm_bytes",3},{"host",6},{"mode","arcade"},
		{"created_at","unused"},{"expires_at","unused"},{"created_unix",now},{"expires_unix",now+600}};
	const std::filesystem::path manifest = "custom-content-lab/session/manifest.json";
	struct ap_verification_session session{};
	char error[512];
	write_json(manifest, valid);
	expect(ap_verification_session_load(manifest.c_str(), &session, error, sizeof error) == 1, "valid fixed manifest loads");
	expect(session.host == 6 && session.mode == 4 && session.lev_bytes == 3, "validated fields cross the C boundary");

	for (const std::string mutation : {"unknown", "expired", "identity", "battle", "escape"}) {
		json hostile = valid;
		if (mutation == std::string("unknown")) hostile["surprise"] = true;
		if (mutation == std::string("expired")) hostile["expires_unix"] = now - 1;
		if (mutation == std::string("identity")) hostile["variant_id"] = std::string(64,'0');
		if (mutation == std::string("battle")) hostile["mode"] = "crystal_challenge";
		if (mutation == std::string("escape")) hostile["lev_path"] = std::filesystem::absolute("outside.lev").string();
		write_json(manifest, hostile);
		expect(ap_verification_session_load(manifest.c_str(), &session, error, sizeof error) == 0,
		       (std::string("hostile manifest refused: ") + mutation).c_str());
	}

	write_json(manifest, valid);
	expect(ap_verification_session_load(manifest.c_str(), &session, error, sizeof error) == 1, "valid manifest reloads after hostile cases");
	expect(ap_verification_session_consume_manifest(manifest.c_str(), "custom-content-lab/session/armed.json",
	                                               error, sizeof error) == 1,
	       "one-use manifest is durably consumed");
	expect(!std::filesystem::exists(manifest) && std::filesystem::exists("custom-content-lab/session/armed.json"),
	       "consumed manifest cannot arm a second process");
	const char *receipt = "custom-content-lab/session/receipt.json";
	expect(ap_verification_session_write_receipt(receipt, &session, std::string(40,'d').c_str(), 1,
	                                             "test", error, sizeof error) == 1, "running executable receipt starts");
	json started; { std::ifstream input(receipt); input >> started; }
	expect(started["complete"] == false && started["executable_sha256"].get<std::string>().size() == 64,
	       "started receipt is incomplete and executable-bound");
	expect(ap_verification_session_finalize_receipt(receipt, error, sizeof error) == 1, "receipt finalizes once");
	json finished; { std::ifstream input(receipt); input >> finished; }
	expect(finished["complete"] == true && finished.contains("end_time_unix"), "final receipt has process end evidence");
	expect(ap_verification_session_finalize_receipt(receipt, error, sizeof error) == 0, "completed receipt cannot be reused");

	std::cout << "verification session: " << checks << " checks, " << failures << " failures\n";
	return failures ? 1 : 0;
}
