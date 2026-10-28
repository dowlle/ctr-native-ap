// Open content-plan admission (#133 PR 2): a package's claimed capability
// flags against the structural report of its exact bytes.
// cc -Wall -Wextra -Werror -DCTR_CUSTOM_TRACKS -I ap -I include -I . tools/test-content-capability.c -o /tmp/test-content-capability
//
// The real verifier is compiled in, so the Arcade and Time Trial reasons and
// the per-mode detection the game uses are the ones under test. Reports are
// built from measurements through the verifier's own derive step; the LEV
// parser itself is covered by tools/test-custom-content-verify.c.

#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <string.h>

#define CTR_CUSTOM_TRACKS 1
#include "platform/native_custom_content_verify.c"
#include "ap/ap_content_capability.h"

static int checks, failures;
static void expect(int condition, const char *name)
{
	checks++;
	if (!condition) { failures++; printf("FAIL %s\n", name); }
}

static struct CustomContentVerification full_track(void)
{
	struct CustomContentVerification r;
	memset(&r, 0, sizeof r);
	r.loadable = 1;
	r.measured.checkpoints = 35;
	r.measured.spawns = 8;
	r.measured.navPaths = 3;
	r.measured.relicCrates = 2;
	r.measured.letterC = r.measured.letterT = r.measured.letterR = 1;
	r.measured.fruitCrates = 2;
	derive(&r);
	return r;
}

static void redo(struct CustomContentVerification *r) { derive(r); }

int main(void)
{
	char why[320];
	struct CustomContentVerification r = full_track();

	expect(AP_ContentMeasuredCapabilities(&r) == AP_CONTENT_CAP_ALL, "a complete track proves all five capabilities");
	expect(AP_ContentCapabilitiesHold(AP_CONTENT_CAP_ALL, &r, why, sizeof why) && why[0] == '\0', "all claims hold");
	expect(AP_ContentCapabilitiesHold(0, &r, why, sizeof why), "claiming nothing always holds on loadable bytes");

	r = full_track(); r.measured.spawns = 7; redo(&r);
	expect(!(AP_ContentMeasuredCapabilities(&r) & AP_CONTENT_CAP_ARCADE), "seven grid slots are not an Arcade race");
	expect(!AP_ContentCapabilitiesHold(AP_CONTENT_CAP_ARCADE, &r, why, sizeof why) && strstr(why, "claims arcade") &&
	       strstr(why, "spawns 7"), "arcade claim refused with the measured grid");
	expect(AP_ContentCapabilitiesHold(AP_CONTENT_CAP_TIME_TRIAL | AP_CONTENT_CAP_WUMPA, &r, why, sizeof why),
	       "a narrower claim on the same bytes holds");

	r = full_track(); r.measured.navPaths = 0; redo(&r);
	expect(!AP_ContentCapabilitiesHold(AP_CONTENT_CAP_ARCADE, &r, why, sizeof why) && strstr(why, "AI paths 0"),
	       "no AI paths refuses arcade");

	r = full_track(); r.measured.checkpoints = 0; redo(&r);
	expect((AP_ContentMeasuredCapabilities(&r) & (AP_CONTENT_CAP_TIME_TRIAL | AP_CONTENT_CAP_ARCADE |
	        AP_CONTENT_CAP_RELIC | AP_CONTENT_CAP_CTR_TOKENS)) == 0, "no lap checkpoints proves no race mode");
	expect(!AP_ContentCapabilitiesHold(AP_CONTENT_CAP_TIME_TRIAL, &r, why, sizeof why) && strstr(why, "time_trial"),
	       "time_trial claim refused without checkpoints");
	expect(AP_ContentCapabilitiesHold(AP_CONTENT_CAP_WUMPA, &r, why, sizeof why), "Wumpa does not need checkpoints");

	r = full_track(); r.measured.letterR = 0; redo(&r);
	expect(!AP_ContentCapabilitiesHold(AP_CONTENT_CAP_CTR_TOKENS, &r, why, sizeof why) && strstr(why, "ctr_tokens") &&
	       strstr(why, "letters 1/1/0"), "a missing R refuses ctr_tokens");

	r = full_track(); r.measured.relicCrates = 0; redo(&r);
	expect(!AP_ContentCapabilitiesHold(AP_CONTENT_CAP_RELIC, &r, why, sizeof why) && strstr(why, "claims relic"),
	       "no time crates refuses relic");

	r = full_track(); r.measured.fruitCrates = 1; r.measured.looseWumpa = 4; redo(&r);
	expect(!AP_ContentCapabilitiesHold(AP_CONTENT_CAP_WUMPA, &r, why, sizeof why) && strstr(why, "claims wumpa"),
	       "nine guaranteed fruit refuses wumpa");
	r.measured.looseWumpa = 5;
	expect(AP_ContentCapabilitiesHold(AP_CONTENT_CAP_WUMPA, &r, why, sizeof why), "ten guaranteed fruit hold wumpa");
	r.measured.fruitCrates = 0; r.measured.looseWumpa = 10;
	expect(AP_ContentCapabilitiesHold(AP_CONTENT_CAP_WUMPA, &r, why, sizeof why), "ten loose fruit hold wumpa");

	r = full_track(); r.measured.spawns = 0; r.measured.navPaths = 0; r.measured.letterT = 0; redo(&r);
	expect(!AP_ContentCapabilitiesHold(AP_CONTENT_CAP_ALL, &r, why, sizeof why) && strstr(why, "claims arcade"),
	       "the first missing flag in fixed order is named");

	r = full_track(); r.loadable = 0;
	expect(AP_ContentMeasuredCapabilities(&r) == 0, "unloadable bytes prove nothing");
	expect(!AP_ContentCapabilitiesHold(0, &r, why, sizeof why) && strstr(why, "structural check"),
	       "unloadable bytes refuse even an empty claim");
	expect(!AP_ContentCapabilitiesHold(0, NULL, why, sizeof why), "no report refuses");

	r = full_track();
	expect(!AP_ContentCapabilitiesHold(0x20u, &r, why, sizeof why) && strstr(why, "unknown capability"),
	       "an unknown claim bit refuses");
	expect(!AP_ContentCapabilitiesHold(AP_CONTENT_CAP_ARCADE | 0x40u, &r, NULL, 0), "no reason buffer is allowed");

	expect(!strcmp(AP_ContentCapabilityName(AP_CONTENT_CAP_TIME_TRIAL), "time_trial") &&
	       !strcmp(AP_ContentCapabilityName(AP_CONTENT_CAP_CTR_TOKENS), "ctr_tokens") &&
	       !strcmp(AP_ContentCapabilityName(AP_CONTENT_CAP_WUMPA), "wumpa"), "capability names match the descriptor keys");

	/* Super turbo pads (#133 PR 4c): a stated value must match the bytes in
	   both directions; an unstated one never refuses. */
	r = full_track(); r.measured.superTurboPads = 2;
	expect(AP_ContentSuperTurboPadsHold(1, &r, why, sizeof why) && why[0] == '\0', "super pads stated and present hold");
	expect(!AP_ContentSuperTurboPadsHold(0, &r, why, sizeof why) && strstr(why, "is false") && strstr(why, "2 super turbo"),
	       "super pads stated false on a track with two refuse");
	expect(AP_ContentSuperTurboPadsHold(AP_CONTENT_SUPER_PADS_UNSTATED, &r, why, sizeof why), "unstated super pads hold");
	r.measured.superTurboPads = 0;
	expect(AP_ContentSuperTurboPadsHold(0, &r, why, sizeof why), "no super pads stated and absent hold");
	expect(!AP_ContentSuperTurboPadsHold(1, &r, why, sizeof why) && strstr(why, "is true") && strstr(why, "0 super turbo"),
	       "super pads stated true on a track without refuse");
	expect(AP_ContentSuperTurboPadsHold(AP_CONTENT_SUPER_PADS_UNSTATED, &r, NULL, 0), "unstated holds without a reason buffer");
	r.loadable = 0;
	expect(!AP_ContentSuperTurboPadsHold(AP_CONTENT_SUPER_PADS_UNSTATED, &r, why, sizeof why) && strstr(why, "structural"),
	       "unloadable bytes refuse even an unstated value");
	expect(!AP_ContentSuperTurboPadsHold(1, NULL, why, sizeof why), "no report refuses");
	expect(!(AP_CONTENT_CAP_ALL & 0x20u), "super pads are not one of the mode flags");

	/* Weapon crates (#133 PR 4e): a stated count must be the measured count,
	   higher or lower; an unstated count never refuses. */
	r = full_track(); r.measured.weaponCrates = 8;
	expect(AP_ContentWeaponCratesHold(8, &r, why, sizeof why) && why[0] == '\0', "the measured weapon crate count holds");
	expect(!AP_ContentWeaponCratesHold(9, &r, why, sizeof why) && strstr(why, "weapon_crates is 9") && strstr(why, "8 weapon crates"),
	       "one crate more than the bytes refuses");
	expect(!AP_ContentWeaponCratesHold(7, &r, why, sizeof why), "one crate fewer refuses");
	expect(!AP_ContentWeaponCratesHold(0, &r, why, sizeof why), "no crates on a track with eight refuses");
	expect(AP_ContentWeaponCratesHold(AP_CONTENT_WEAPON_CRATES_UNSTATED, &r, why, sizeof why), "an unstated count holds");
	expect(!AP_ContentWeaponCratesHold(-2, &r, why, sizeof why), "a negative count refuses");
	r.measured.weaponCrates = 0;
	expect(AP_ContentWeaponCratesHold(0, &r, NULL, 0), "no crates stated and none measured hold");
	r.loadable = 0;
	expect(!AP_ContentWeaponCratesHold(AP_CONTENT_WEAPON_CRATES_UNSTATED, &r, why, sizeof why) && strstr(why, "structural"),
	       "unloadable bytes refuse even an unstated count");
	expect(!AP_ContentWeaponCratesHold(0, NULL, why, sizeof why), "no report refuses");

	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
