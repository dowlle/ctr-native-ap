// Native parser for the `win_logic` slot_data block (native #449, race-loss
// DeathLink stakes, block version 1). Compiles the REAL parser (ap/ap_seedcfg.cpp
// is linked in).
//
//   c++ -m32 -std=c++17 -Wall -DCTR_AP -DAP_WIN_LOGIC_FREESTANDING_ONLY -Iap -Iap/vendor/json/include
//     tools/test-win-logic-seedcfg.cpp ap/ap_seedcfg.cpp
//     -o /tmp/test-win-logic-seedcfg && /tmp/test-win-logic-seedcfg
//
// What this pins (SCHEMA.md "Term encoding", "Invalid or missing block"):
//   * the schema's own example parses VALID with the right tables;
//   * absent block -> ABSENT, version > 1 -> NEWER, every malformation the
//     schema lists -> INVALID with all tables dropped (never partially used);
//   * a second seed never inherits the first seed's block;
//   * refs resolve to their check, a ref whose target itself has a ref refuses.

#include <cstdio>
#include <cstring>
#include <string>
#include <nlohmann/json.hpp>

#include "../ap/ap_seedcfg.h"
#include "../ap/ap_win_logic.h"

extern void ap_seedcfg_parse_json(const nlohmann::json &j);
static std::string g_last_log;
extern "C" void AP_LogLine(const char *m) { g_last_log += m; }

static int checks;
static int failures;

static void expect(bool ok, const char *name)
{
	checks++;
	if (!ok)
	{
		failures++;
		std::printf("FAIL %s\n", name);
	}
}

static void expect_int(long got, long want, const char *name)
{
	checks++;
	if (got != want)
	{
		failures++;
		std::printf("FAIL %s (got %ld, want %ld)\n", name, got, want);
	}
}

static nlohmann::json seed_with(const nlohmann::json *block)
{
	nlohmann::json j = {{"ctr_options", {{"schema_version", 16}, {"death_link", 3}}}};
	if (block)
		j["win_logic"] = *block;
	return j;
}

// SCHEMA.md "Example (abbreviated)", verbatim.
static const char *kExample = R"JSON({
  "version": 1,
  "families": [[35010101], [35010097, 35010098], [35010095, 35010096], [35010103], [35010104]],
  "regions": [
    true,
    ["all", ["req", 2, 1, -1], ["req", 3, 2, 0]],
    ["all", ["req", 2, 2, -1], ["items", "sum", 1, [35010123]]]
  ],
  "checks": {
    "35011002": {"kind": "trophy", "region": 0, "rule": true},
    "35012002": {"kind": "sapphire", "region": 0, "rule": ["ref", 35011002]},
    "35012102": {"kind": "gold", "region": 0, "rule": ["all", ["ref", 35011002], ["cap", 1, -1]]},
    "35012302": {"kind": "ctr", "region": 0, "rule": ["all", ["ref", 35011002], ["tokens_no_purple", 4], ["cap", 1, -1]]},
    "35011012": {"kind": "trophy", "region": 2, "rule": ["any", ["cap", 1, 0], ["families", 3]]}
  }
})JSON";

static nlohmann::json example(void)
{
	return nlohmann::json::parse(kExample);
}

static void parse_block(const nlohmann::json &b)
{
	g_last_log.clear();
	ap_seedcfg_parse_json(seed_with(&b));
}

static void expect_invalid(const nlohmann::json &b, const char *name, const char *problemPart)
{
	parse_block(b);
	char label[256];
	std::snprintf(label, sizeof label, "%s -> INVALID", name);
	expect_int(ctr_win_logic.state, AP_WL_INVALID, label);
	std::snprintf(label, sizeof label, "%s -> tables dropped", name);
	expect(ctr_win_logic.check_count == 0 && ctr_win_logic.node_count == 0 &&
	           ctr_win_logic.region_count == 0 && ctr_win_logic.family_count == 0,
	       label);
	std::snprintf(label, sizeof label, "%s -> problem names \"%s\" (got \"%s\")", name,
	              problemPart, ctr_win_logic.problem);
	expect(std::strstr(ctr_win_logic.problem, problemPart) != nullptr, label);
	std::snprintf(label, sizeof label, "%s -> one refusal log line", name);
	expect(g_last_log.find("[AP CFG] win_logic refused (") != std::string::npos, label);
}

// Replace the rule of check 35011002 in the example.
static nlohmann::json with_rule(const nlohmann::json &rule)
{
	nlohmann::json b = example();
	b["checks"]["35011002"]["rule"] = rule;
	return b;
}

int main(void)
{
	// ── absent ──
	ap_seedcfg_parse_json(seed_with(nullptr));
	expect_int(ctr_win_logic.state, AP_WL_ABSENT, "no block -> ABSENT");
	expect(ctr_cfg_active() != 0, "config still active without the block");

	// ── the schema's example ──
	parse_block(example());
	const AP_WinLogic &wl = ctr_win_logic;
	expect_int(wl.state, AP_WL_VALID, "example -> VALID");
	expect_int(wl.version, 1, "version 1");
	expect_int(wl.family_count, 5, "5 families");
	expect_int(wl.family_len[1], 2, "family 1 has two ids");
	expect_int(wl.ids[wl.family_first[1] + 1], 35010098L, "family 1 second id");
	expect_int(wl.region_count, 3, "3 regions");
	expect_int(wl.nodes[wl.region_root[0]].tag, AP_WL_T_TRUE, "region 0 is true");
	expect_int(wl.nodes[wl.region_root[1]].tag, AP_WL_T_ALL, "region 1 is all");
	expect_int(wl.nodes[wl.region_root[1]].b, 2, "region 1 has two children");
	expect_int(wl.check_count, 5, "5 checks");
	for (int i = 1; i < wl.check_count; i++)
		expect(wl.checks[i - 1].code < wl.checks[i].code, "checks sorted by code");
	{
		int t = AP_WinLogicFindCheck(&wl, 35011012L);
		expect(t >= 0, "35011012 found");
		expect_int(wl.checks[t].kind, AP_WL_KIND_TROPHY, "35011012 kind trophy");
		expect_int(wl.checks[t].region, 2, "35011012 region 2");
		int g = AP_WinLogicFindCheck(&wl, 35012102L);
		expect_int(wl.checks[g].kind, AP_WL_KIND_GOLD, "35012102 kind gold");
		const AP_WinLogicNode &all = wl.nodes[wl.checks[g].rule];
		expect_int(all.tag, AP_WL_T_ALL, "gold rule is all");
		const AP_WinLogicNode &ref = wl.nodes[wl.kids[all.a]];
		expect_int(ref.tag, AP_WL_T_REF, "first child is ref");
		expect_int(ref.b, AP_WinLogicFindCheck(&wl, 35011002L), "ref resolved to 35011002's index");
		const AP_WinLogicNode &cap = wl.nodes[wl.kids[all.a + 1]];
		expect(cap.tag == AP_WL_T_CAP && cap.a == 1 && cap.b == -1, "cap 1, -1");
		expect_int(AP_WinLogicFindCheck(&wl, 35011003L), -1, "absent location has no entry");
	}
	expect(g_last_log.find("[AP CFG] win_logic v1: 5 win check(s)") != std::string::npos,
	       "valid block logs its summary");

	// ── every leaf kind parses ──
	{
		nlohmann::json rule = nlohmann::json::array(
		    {"all", nlohmann::json::array({"req", 8, 3, -1}),
		     nlohmann::json::array({"tokens_no_purple", 2}),
		     nlohmann::json::array({"items", "distinct", 2, {35010139, 35010140, 35010141}}),
		     nlohmann::json::array({"items", "sum", 1, {35010123}}),
		     nlohmann::json::array({"cap", 3, 15}), nlohmann::json::array({"bosses", 4}),
		     nlohmann::json::array({"families", 0}), nlohmann::json::array({"any"}),
		     nlohmann::json::array({"all"}), false});
		parse_block(with_rule(rule));
		expect_int(ctr_win_logic.state, AP_WL_VALID, "every leaf kind -> VALID");
		int c = AP_WinLogicFindCheck(&ctr_win_logic, 35011002L);
		const AP_WinLogicNode &n = ctr_win_logic.nodes[ctr_win_logic.checks[c].rule];
		expect_int(n.b, 10, "all has 10 children");
		int tags[10] = {AP_WL_T_REQ, AP_WL_T_TOKENS_NO_PURPLE, AP_WL_T_ITEMS_DISTINCT,
		                AP_WL_T_ITEMS_SUM, AP_WL_T_CAP, AP_WL_T_BOSSES, AP_WL_T_FAMILIES,
		                AP_WL_T_ANY, AP_WL_T_ALL, AP_WL_T_FALSE};
		for (int i = 0; i < 10; i++)
			expect_int(ctr_win_logic.nodes[ctr_win_logic.kids[n.a + i]].tag, tags[i], "leaf tag order");
		const AP_WinLogicNode &it = ctr_win_logic.nodes[ctr_win_logic.kids[n.a + 2]];
		expect(it.a == 2 && it.c == 3 && ctr_win_logic.ids[it.b + 2] == 35010141L, "items fields");
	}

	// ── extra keys are tolerated (additive fields never refuse a block) ──
	{
		nlohmann::json b = example();
		b["note"] = "extra";
		b["checks"]["35011002"]["extra"] = 1;
		parse_block(b);
		expect_int(ctr_win_logic.state, AP_WL_VALID, "unknown extra keys -> still VALID");
	}

	// ── version ──
	{
		nlohmann::json b = example();
		b["version"] = 2;
		b["checks"] = "anything";
		parse_block(b);
		expect_int(ctr_win_logic.state, AP_WL_NEWER, "version 2 -> NEWER (treated as absent)");
		expect(g_last_log.find("newer than this client") != std::string::npos, "NEWER logged once");
	}
	{
		nlohmann::json b = example();
		b.erase("version");
		expect_invalid(b, "missing version", "version");
		b["version"] = 0;
		expect_invalid(b, "version 0", "version");
		b["version"] = true;
		expect_invalid(b, "version true", "version");
		b["version"] = "1";
		expect_invalid(b, "version \"1\"", "version");
		b["version"] = 1.0;
		expect_invalid(b, "version 1.0", "version");
	}

	// ── block shape ──
	expect_invalid(nlohmann::json::array(), "block is an array", "not an object");
	{
		nlohmann::json b = example();
		b.erase("families");
		expect_invalid(b, "missing families", "families");
		b = example();
		b["families"] = {{35010101}, {"x"}};
		expect_invalid(b, "family id not an int", "family id");
		b = example();
		b["families"] = {35010101};
		expect_invalid(b, "family not an array", "family is not an array");
		b = example();
		b.erase("regions");
		expect_invalid(b, "missing regions", "regions");
		b = example();
		b.erase("checks");
		expect_invalid(b, "missing checks", "checks");
		b = example();
		b["checks"] = nlohmann::json::array();
		expect_invalid(b, "checks is an array", "checks");
	}

	// ── check entries ──
	{
		const char *badKeys[] = {"035011002", "+35011002", " 35011002", "35011002 ", "-1",
		                         "0", "3501100x", "", "99999999999"};
		for (const char *k : badKeys)
		{
			nlohmann::json b = example();
			b["checks"][k] = {{"kind", "trophy"}, {"region", 0}, {"rule", true}};
			expect_invalid(b, (std::string("bad key \"") + k + "\"").c_str(), "canonical");
		}
		nlohmann::json b = example();
		b["checks"]["35011002"]["kind"] = "podium";
		expect_invalid(b, "unknown kind", "kind");
		b = example();
		b["checks"]["35011002"].erase("kind");
		expect_invalid(b, "missing kind", "kind");
		b = example();
		b["checks"]["35011002"]["region"] = 3;
		expect_invalid(b, "region out of range", "region is missing or out of range");
		b = example();
		b["checks"]["35011002"]["region"] = -1;
		expect_invalid(b, "negative region", "region is missing or out of range");
		b = example();
		b["checks"]["35011002"]["region"] = "0";
		expect_invalid(b, "region as string", "region is missing or out of range");
		b = example();
		b["checks"]["35011002"].erase("rule");
		expect_invalid(b, "missing rule", "rule is missing");
		b = example();
		b["checks"]["35011002"] = true;
		expect_invalid(b, "check entry not an object", "not an object");
	}

	// ── terms ──
	expect_invalid(with_rule(nlohmann::json::array({"nope", 1})), "unknown tag", "unknown term tag");
	expect_invalid(with_rule(1), "integer term", "term is not");
	expect_invalid(with_rule("true"), "string term", "term is not");
	expect_invalid(with_rule(nlohmann::json::array()), "empty array term", "term is not");
	expect_invalid(with_rule(nlohmann::json::array({1, 2})), "untagged array", "term is not");
	expect_invalid(with_rule(nlohmann::json::object()), "object term", "term is not");
	expect_invalid(with_rule(nlohmann::json::array({"req", 2, 1})), "req arity", "req has wrong arity");
	expect_invalid(with_rule(nlohmann::json::array({"req", 0, 1, -1})), "req type 0", "req type");
	expect_invalid(with_rule(nlohmann::json::array({"req", 9, 1, -1})), "req type 9", "req type");
	expect_invalid(with_rule(nlohmann::json::array({"req", 2, -1, -1})), "req negative count", "req count");
	expect_invalid(with_rule(nlohmann::json::array({"req", 2, 1, 5})), "req colour 5", "req colour");
	expect_invalid(with_rule(nlohmann::json::array({"req", 2, true, -1})), "req bool count", "req count");
	expect_invalid(with_rule(nlohmann::json::array({"tokens_no_purple"})), "tokens arity", "wrong arity");
	expect_invalid(with_rule(nlohmann::json::array({"tokens_no_purple", -2})), "tokens negative", "count");
	expect_invalid(with_rule(nlohmann::json::array({"bosses", 1, 2})), "bosses arity", "wrong arity");
	expect_invalid(with_rule(nlohmann::json::array({"families", 1.5})), "families float", "count");
	expect_invalid(with_rule(nlohmann::json::array({"items", "max", 1, {35010123}})), "items mode", "items mode");
	expect_invalid(with_rule(nlohmann::json::array({"items", "sum", 1, nlohmann::json::array()})),
	               "items empty ids", "non-empty");
	expect_invalid(with_rule(nlohmann::json::array({"items", "sum", 1, {35010123, 35010123}})),
	               "items duplicate ids", "duplicate");
	expect_invalid(with_rule(nlohmann::json::array({"items", "sum", 1, {"35010123"}})),
	               "items string id", "items id");
	expect_invalid(with_rule(nlohmann::json::array({"items", "sum", 1})), "items arity", "wrong arity");
	expect_invalid(with_rule(nlohmann::json::array({"cap", 4, -1})), "cap boost 4", "cap boost");
	expect_invalid(with_rule(nlohmann::json::array({"cap", 1, 16})), "cap racer 16", "cap racer");
	expect_invalid(with_rule(nlohmann::json::array({"cap", 1, -2})), "cap racer -2", "cap racer");
	expect_invalid(with_rule(nlohmann::json::array({"cap", 1})), "cap arity", "wrong arity");
	expect_invalid(with_rule(nlohmann::json::array({"all", true, nlohmann::json::array({"bad"})})),
	               "bad child inside all", "unknown term tag");
	expect_invalid(with_rule(nlohmann::json::array({"ref", 35099999})), "ref to absent location",
	               "not in checks");
	expect_invalid(with_rule(nlohmann::json::array({"ref", "35012002"})), "ref as string", "ref location");
	expect_invalid(with_rule(nlohmann::json::array({"ref", 35012002})),
	               "ref to a check whose rule has a ref", "itself contains a ref");
	{
		// ref target whose REGION contains a ref: also depth 2, refused.
		nlohmann::json b = example();
		b["regions"].push_back(nlohmann::json::array({"ref", 35011002}));
		b["checks"]["35011012"]["region"] = 3;
		b["checks"]["35012002"]["rule"] = nlohmann::json::array({"ref", 35011012});
		expect_invalid(b, "ref target region has a ref", "itself contains a ref");
	}
	{
		nlohmann::json deep = true;
		for (int i = 0; i < AP_WL_MAX_DEPTH + 2; i++)
			deep = nlohmann::json::array({"all", deep});
		expect_invalid(with_rule(deep), "nesting past the guard", "nested too deeply");
		nlohmann::json ok = true;
		for (int i = 0; i < 16; i++)
			ok = nlohmann::json::array({"any", ok});
		parse_block(with_rule(ok));
		expect_int(ctr_win_logic.state, AP_WL_VALID, "16 levels of nesting -> VALID");
	}
	{
		nlohmann::json b = example();
		b["regions"][1] = nlohmann::json::array({"req", 2});
		expect_invalid(b, "bad region term", "region 1: req has wrong arity");
	}

	// ── a second seed never inherits the first seed's block ──
	parse_block(example());
	expect_int(ctr_win_logic.state, AP_WL_VALID, "seed A valid");
	ap_seedcfg_parse_json(seed_with(nullptr));
	expect_int(ctr_win_logic.state, AP_WL_ABSENT, "seed B without block -> ABSENT");
	expect_int(ctr_win_logic.check_count, 0, "seed B has no checks");
	parse_block(example());
	ap_seedcfg_parse_json(nlohmann::json{{"win_logic", example()}});
	expect_int(ctr_win_logic.state, AP_WL_ABSENT, "no ctr_options (inactive config) -> ABSENT");

	// ── a large block (about 40 KiB of JSON) fits ──
	{
		nlohmann::json b = example();
		nlohmann::json big = nlohmann::json::array({"any"});
		for (int i = 0; i < 2500; i++)
			big.push_back(nlohmann::json::array({"req", 1 + i % 8, i, -1}));
		b["checks"]["35011002"]["rule"] = big;
		size_t bytes = b.dump().size();
		parse_block(b);
		expect(bytes > 40000, "large block is over 40 KiB");
		expect_int(ctr_win_logic.state, AP_WL_VALID, "large block -> VALID");
	}

	std::printf("%s: %d checks, %d failure(s)\n", failures ? "FAIL" : "PASS", checks, failures);
	return failures ? 1 : 0;
}
