// cc -Wall -Wextra -I include tools/test-custom-state-identity.c -o /tmp/test-custom-state-identity
//
// Savestate identity for custom tracks (box authoring build,
// native_custom_state_identity.h): a checkpoint restores only into the load
// context it was saved in. No custom track on both sides, or the same package
// by UUID, LEV and VRM SHA-256. Everything else refuses, in both directions,
// including an unusable ("unknown") or damaged record.
#include <stdio.h>
#include <string.h>

#include <platform/native_custom_state_identity.h>

static int checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)

#define UUID_A "0f8fad5b-d9cb-469f-a165-70867728950e"
#define UUID_B "7c9e6679-7425-40de-944b-e07fc1f90ae7"
#define SHA_1 "1111111111111111111111111111111111111111111111111111111111111111"
#define SHA_2 "2222222222222222222222222222222222222222222222222222222222222222"
#define SHA_3 "abcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcd"

static int allowed(const struct CustomStateIdentity *saved, const struct CustomStateIdentity *live)
{
	return CustomStateIdentity_RestoreAllowed(saved, live);
}

int main(void)
{
	struct CustomStateIdentity none1, none2, a, a2, aUpper, b, aLev, aVrm, unk1, unk2, bad, damaged;
	char text[160];

	CustomStateIdentity_None(&none1);
	CustomStateIdentity_None(&none2);
	CHECK(CustomStateIdentity_Package(&a, UUID_A, SHA_1, SHA_2) == 1);
	CHECK(CustomStateIdentity_Package(&a2, UUID_A, SHA_1, SHA_2) == 1);
	CHECK(CustomStateIdentity_Package(&b, UUID_B, SHA_1, SHA_2) == 1);
	CHECK(CustomStateIdentity_Package(&aLev, UUID_A, SHA_3, SHA_2) == 1);
	CHECK(CustomStateIdentity_Package(&aVrm, UUID_A, SHA_1, SHA_3) == 1);
	CustomStateIdentity_Unknown(&unk1);
	CustomStateIdentity_Unknown(&unk2);

	// The record is a fixed-size checkpoint region; CSD2 added the race mode.
	CHECK(sizeof(struct CustomStateIdentity) == 188);
	CHECK(CTR_CUSTOM_STATE_MAGIC == 0x32445343u);
	CHECK(a.mode == 0 && none1.mode == 0);

	// Same context restores.
	CHECK(allowed(&none1, &none2));
	CHECK(allowed(&a, &a2));
	CHECK(allowed(&a2, &a));

	// Trigger A: a custom-race state into a retail race, a menu or a fresh boot.
	CHECK(!allowed(&a, &none1));
	// Trigger B: a retail state into a custom race.
	CHECK(!allowed(&none1, &a));
	// Trigger C: package A's state while package B is loaded, either way.
	CHECK(!allowed(&a, &b));
	CHECK(!allowed(&b, &a));
	// Same UUID, other geometry (a new revision of the same track).
	CHECK(!allowed(&a, &aLev));
	CHECK(!allowed(&aLev, &a));
	CHECK(!allowed(&a, &aVrm));
	CHECK(!allowed(&aVrm, &a));

	// Same package, other race mode (CustomOffline_RuntimeStateIdentity sets the mode):
	// a Time Trial state never restores into an Arcade race of the same track,
	// or the other way round, because the restored game mode would disagree
	// with the runtime and the host slot would lose the custom identity.
	{
		struct CustomStateIdentity arcade = a, arcade2 = a2, trial = a, trial2 = a2;
		arcade.mode = arcade2.mode = 1;
		trial.mode = trial2.mode = 2;
		CHECK(allowed(&arcade, &arcade2));
		CHECK(allowed(&trial, &trial2));
		CHECK(!allowed(&arcade, &trial));
		CHECK(!allowed(&trial, &arcade));
		CustomStateIdentity_Describe(&trial, text, sizeof text);
		CHECK(strcmp(text, "custom " UUID_A " lev 111111111111 vrm 222222222222 time trial") == 0);
		CustomStateIdentity_Describe(&arcade, text, sizeof text);
		CHECK(strcmp(text, "custom " UUID_A " lev 111111111111 vrm 222222222222 arcade") == 0);
	}

	// Case does not split one package into two.
	CHECK(CustomStateIdentity_Package(&aUpper, "0F8FAD5B-D9CB-469F-A165-70867728950E", SHA_1, SHA_2) == 1);
	CHECK(allowed(&aUpper, &a));

	// An unusable manifest is "unknown" and never restores, not even into itself.
	CHECK(!allowed(&unk1, &unk2));
	CHECK(!allowed(&unk1, &none1));
	CHECK(!allowed(&none1, &unk1));
	CHECK(!allowed(&unk1, &a));
	CHECK(!allowed(&a, &unk1));
	CHECK(CustomStateIdentity_Package(&bad, "not-a-uuid", SHA_1, SHA_2) == 0);
	CHECK(bad.kind == CTR_CUSTOM_STATE_UNKNOWN);
	CHECK(CustomStateIdentity_Package(&bad, UUID_A, "1234", SHA_2) == 0);
	CHECK(bad.kind == CTR_CUSTOM_STATE_UNKNOWN);
	CHECK(CustomStateIdentity_Package(&bad, UUID_A, SHA_1, NULL) == 0);
	CHECK(bad.kind == CTR_CUSTOM_STATE_UNKNOWN);
	CHECK(CustomStateIdentity_Package(&bad, "0f8fad5b+d9cb-469f-a165-70867728950e", SHA_1, SHA_2) == 0);
	CHECK(CustomStateIdentity_Package(&bad, UUID_A, SHA_1,
	                                  "zz22222222222222222222222222222222222222222222222222222222222222") == 0);
	CHECK(!allowed(&bad, &bad));

	// A damaged record (wrong magic, unknown kind, or all zero, as in a state
	// from before this region existed) refuses.
	damaged = a;
	damaged.magic = 0;
	CHECK(!allowed(&damaged, &a));
	CHECK(!allowed(&a, &damaged));
	damaged = none1;
	damaged.kind = 7;
	CHECK(!allowed(&damaged, &none1));
	memset(&damaged, 0, sizeof damaged);
	CHECK(!allowed(&damaged, &none1));
	CHECK(!allowed(NULL, &none1));
	CHECK(!allowed(&none1, NULL));

	// Log text.
	CustomStateIdentity_Describe(&none1, text, sizeof text);
	CHECK(strcmp(text, "none") == 0);
	CustomStateIdentity_Describe(&unk1, text, sizeof text);
	CHECK(strcmp(text, "unknown") == 0);
	CustomStateIdentity_Describe(&damaged, text, sizeof text);
	CHECK(strcmp(text, "damaged") == 0);
	CustomStateIdentity_Describe(&aLev, text, sizeof text);
	CHECK(strcmp(text, "custom " UUID_A " lev abcdefabcdef vrm 222222222222") == 0);
	CustomStateIdentity_Describe(&a, text, 10);
	CHECK(strcmp(text, "custom 0f") == 0);
	// Bounded even when the stored strings are not terminated.
	memset(damaged.uuid, 'x', sizeof damaged.uuid);
	memset(damaged.levSha256, 'y', sizeof damaged.levSha256);
	memset(damaged.vrmSha256, 'z', sizeof damaged.vrmSha256);
	damaged.magic = CTR_CUSTOM_STATE_MAGIC;
	damaged.kind = CTR_CUSTOM_STATE_PACKAGE;
	CustomStateIdentity_Describe(&damaged, text, sizeof text);
	CHECK(strlen(text) == 7 + 36 + 5 + 12 + 5 + 12);

	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
