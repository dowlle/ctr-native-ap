/*
 * Host harness for the AP Trophy podium presentation (issue #235).
 *
 *   cc -Wall -Wextra -Werror -DCTR_AP -I ap -I . -I include \
 *      -o /tmp/test-podium-trophy-presentation \
 *      tools/test-podium-trophy-presentation.c && \
 *      /tmp/test-podium-trophy-presentation
 *
 * Behavioral assertions on production code, no source-string checks:
 *
 *   - The freestanding policy game/233/CS_Podium.c consumes
 *     (ap/ap_podium_presentation_logic.h) suppresses the retail prize for an AP
 *     Trophy presentation and keeps it for a non-AP Trophy and every other
 *     reward, so an AP Trophy creates no prize Instance/thread and never runs
 *     the INC_TROPHY count-up while a non-AP Trophy still does both.
 *   - The same policy's exit teardown (AP_PodiumExitWork) reproduces exactly
 *     the work the missing prize thread performed in CS_Podium_Prize_ThTick3:
 *     request overlay transition 2, clear VEH_FREEZE_PODIUM, play the completion
 *     FX, once per AP Trophy podium, and none of it for a non-AP Trophy, an AP
 *     relic, or an AP trial/Cortex Trophy that keeps its prize.
 *   - The shared #330 formatter (ap/ap_reward_text.h) that the podium caller
 *     uses still builds the own, foreign, unknown-fallback and long-recipient
 *     "HOOKSHOT" cases.
 */

#include <stdio.h>
#include <string.h>

#include "../ap/ap_podium_presentation_logic.h"
#include "../ap/ap_reward_text.h"

// Engine model ids the podium can carry (namespace_Instance.h). The trophy id is
// also pinned inside ap_hooks.c with CTR_STATIC_ASSERT against STATIC_TROPHY.
#define TEST_STATIC_BIG1   0x38
#define TEST_STATIC_GEM    0x5f
#define TEST_STATIC_RELIC  0x61
#define TEST_STATIC_KEY    0x63
#define TEST_STATIC_TOKEN  0x7d
#define TEST_NOFUNC        0x00

static int g_failures;

static void expect(int got, int want, const char *what)
{
	printf("%-4s %s (got %d, want %d)\n", got == want ? "ok" : "FAIL", what,
	       got, want);
	if (got != want)
		g_failures++;
}

static void expect_sentence(const char *item, const char *player, int own,
                            const char *want, const char *what)
{
	char out[64];
	int ok = AP_RewardTextBuild(out, (int)sizeof out, item, player, own);

	if (!ok)
	{
		printf("FAIL %s: no sentence, wanted \"%s\"\n", what, want);
		g_failures++;
		return;
	}
	if (strcmp(out, want) != 0)
	{
		printf("FAIL %s: got \"%s\", wanted \"%s\"\n", what, out, want);
		g_failures++;
		return;
	}
	printf("ok   %s: \"%s\"\n", what, out);
}

// Minimal model of the engine state the podium exit work touches, so the
// production decision can be driven and observed host-side.
typedef struct
{
	int overlayTransition;
	int freezePodium;
	int completionSounds;
} exit_effects;

static void apply_exit_work(const ap_podium_exit_work *work, exit_effects *fx)
{
	if (work->overlayTransition != 0)
		fx->overlayTransition = work->overlayTransition;
	if (work->releaseFreeze)
		fx->freezePodium = 0;
	if (work->completionSound)
		fx->completionSounds++;
}

int main(void)
{
	// ── The AP Trophy presentation owns the podium ──────────────────────────
	// AP active + ordinary retail Trophy -> no retail prize, so no INC_TROPHY
	// count-up.
	expect(AP_PodiumIsApTrophyPresentation(1, AP_PODIUM_TROPHY_MODEL, 0), 1,
	       "AP Trophy is an AP presentation");
	expect(AP_PodiumShouldBirthPrize(1, AP_PODIUM_TROPHY_MODEL, 0), 0,
	       "AP Trophy creates no prize object");

	// A non-AP Trophy keeps the retail prize and its count-up.
	expect(AP_PodiumIsApTrophyPresentation(0, AP_PODIUM_TROPHY_MODEL, 0), 0,
	       "non-AP Trophy is not an AP presentation");
	expect(AP_PodiumShouldBirthPrize(0, AP_PODIUM_TROPHY_MODEL, 0), 1,
	       "non-AP Trophy still creates the prize object");

	// A trial-track or Cortex Vortex podium reuses STATIC_TROPHY without a
	// retail trophy bit, so it keeps the vanilla prize path.
	expect(AP_PodiumShouldBirthPrize(1, AP_PODIUM_TROPHY_MODEL, 1), 1,
	       "AP special-track Trophy keeps the prize");

	// Every other reward, AP or not, keeps its vanilla prize path.
	expect(AP_PodiumShouldBirthPrize(1, TEST_STATIC_RELIC, 0), 1,
	       "AP Relic keeps the prize");
	expect(AP_PodiumShouldBirthPrize(1, TEST_STATIC_KEY, 0), 1,
	       "AP Key keeps the prize");
	expect(AP_PodiumShouldBirthPrize(1, TEST_STATIC_GEM, 0), 1,
	       "AP Gem keeps the prize");
	expect(AP_PodiumShouldBirthPrize(1, TEST_STATIC_TOKEN, 0), 1,
	       "AP Token keeps the prize");
	expect(AP_PodiumShouldBirthPrize(1, TEST_STATIC_BIG1, 0), 1,
	       "Oxide BIG1 keeps the prize");
	expect(AP_PodiumShouldBirthPrize(1, TEST_NOFUNC, 0), 1,
	       "NOFUNC keeps the prize");
	expect(AP_PodiumShouldBirthPrize(0, TEST_STATIC_RELIC, 0), 1,
	       "non-AP Relic keeps the prize");

	// ── Podium exit teardown the skipped prize thread would have performed ───
	// CS_Podium_Prize_ThTick3 raises overlayTransition to 2, clears
	// VEH_FREEZE_PODIUM and plays the completion FX (0x67) when the ceremony
	// ends. The AP ordinary-Trophy presentation births no prize thread, so the
	// exit must reproduce that work exactly once and only on that path.
	{
		ap_podium_exit_state state;
		ap_podium_exit_work work;
		exit_effects fx;

		AP_PodiumExitStateReset(&state);
		fx.overlayTransition = 0;
		fx.freezePodium = 1; // as CS_Podium_FullScene_Init sets VEH_FREEZE_PODIUM
		fx.completionSounds = 0;

		work = AP_PodiumExitWork(1, AP_PODIUM_TROPHY_MODEL, 0, &state);
		expect(work.overlayTransition, 2, "AP Trophy exit requests overlay 2");
		expect(work.releaseFreeze, 1, "AP Trophy exit releases the freeze");
		expect(work.completionSound, 1, "AP Trophy exit plays the completion FX");
		apply_exit_work(&work, &fx);
		expect(fx.freezePodium, 0, "AP Trophy exit cannot strand the player");
		expect(fx.overlayTransition, 2, "AP Trophy exit ends at transition 2");

		// The camera thread dies on the continue press, so a defensive second
		// drive must be a no-op: the work is one-shot per podium.
		work = AP_PodiumExitWork(1, AP_PODIUM_TROPHY_MODEL, 0, &state);
		expect(work.overlayTransition, 0, "AP Trophy exit is one-shot (overlay)");
		expect(work.releaseFreeze, 0, "AP Trophy exit is one-shot (freeze)");
		expect(work.completionSound, 0, "AP Trophy exit is one-shot (sound)");
		apply_exit_work(&work, &fx);
		expect(fx.completionSounds, 1, "completion FX plays exactly once");
		expect(fx.overlayTransition, 2, "second drive does not re-transition");
		expect(fx.freezePodium, 0, "second drive keeps the player released");

		// A newly born podium re-arms the one-shot.
		AP_PodiumExitStateReset(&state);
		work = AP_PodiumExitWork(1, AP_PODIUM_TROPHY_MODEL, 0, &state);
		expect(work.overlayTransition, 2, "next AP Trophy podium re-arms");

		// A non-AP Trophy, an AP relic, and an AP trial/Cortex Trophy (which
		// reuses the Trophy model but keeps its prize thread) perform none of
		// the new exit work; their own prize thread still owns the teardown.
		AP_PodiumExitStateReset(&state);
		work = AP_PodiumExitWork(0, AP_PODIUM_TROPHY_MODEL, 0, &state);
		expect(work.overlayTransition, 0, "non-AP Trophy does no exit work");
		expect(work.releaseFreeze, 0, "non-AP Trophy keeps its prize thread work");

		work = AP_PodiumExitWork(1, TEST_STATIC_RELIC, 0, &state);
		expect(work.overlayTransition, 0, "AP relic does no exit work");
		expect(work.releaseFreeze, 0, "AP relic keeps its prize thread work");
		expect(work.completionSound, 0, "AP relic plays no extra FX");

		work = AP_PodiumExitWork(1, AP_PODIUM_TROPHY_MODEL, 1, &state);
		expect(work.overlayTransition, 0, "AP special-track Trophy does no exit work");
		expect(work.releaseFreeze, 0, "AP special-track Trophy keeps its prize");
	}

	// ── Shared #330 formatter integration on the podium caller path ──────────
	expect_sentence("Moon Pearl", "Local", 1, "HAVE A MOON PEARL.",
	                "own item");
	expect_sentence("Moon Pearl", "Alice", 0, "HAVE ALICE'S MOON PEARL.",
	                "foreign item");

	// Unknown / missing scout metadata falls back to the retail string, which
	// the podium caller passes as the retail "win a trophy" line.
	{
		char out[64];
		expect(AP_RewardTextBuild(out, (int)sizeof out, "Unknown", "Alice", 0),
		       0, "unknown scout falls back to retail");
	}

	// Long recipient with a short item must keep the possessive and the item.
	{
		char longPlayer[101];
		char out[64];
		memset(longPlayer, 'p', sizeof longPlayer - 1);
		longPlayer[sizeof longPlayer - 1] = '\0';

		if (!AP_RewardTextBuild(out, (int)sizeof out, "HOOKSHOT", longPlayer, 0))
		{
			printf("FAIL long recipient: no sentence\n");
			g_failures++;
		}
		else
		{
			expect(strstr(out, "HOOKSHOT") != NULL, 1,
			       "long recipient keeps HOOKSHOT");
			expect(strstr(out, "'S ") != NULL, 1,
			       "long recipient keeps the possessive");
		}
	}

	if (g_failures)
	{
		printf("podium trophy presentation: FAIL (%d)\n", g_failures);
		return 1;
	}
	puts("podium trophy presentation: PASS");
	return 0;
}
