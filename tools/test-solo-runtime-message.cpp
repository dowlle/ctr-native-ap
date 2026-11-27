// Host harness for the Play Solo runtime problem message (PS1): which sentence
// the menu shows when the solo runtime, its Python or the bundled ctr.apworld
// is missing, and that the wrapper's own "ctr.apworld is missing: <path>"
// error reaches the screen as the same plain sentence (the log keeps the path).
#include "ap_solo_logic.hpp"

#include <cstdio>
#include <string>

static int g_fail = 0;
#define CHECK(cond)                                                       \
	do                                                                    \
	{                                                                     \
		if (!(cond))                                                      \
		{                                                                 \
			std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
			g_fail++;                                                     \
		}                                                                 \
	} while (0)

int main()
{
	const std::string apworld =
	    "ctr.apworld is missing from the game folder. Put it back next to the game: copy it, do not move it.";
	CHECK(AP_SoloRuntimeProblemText(true, true, true).empty());
	CHECK(AP_SoloRuntimeProblemText(true, true, false) == apworld);
	CHECK(AP_SoloRuntimeProblemText(false, true, true) ==
	      "Python is missing from the solo-runtime folder. Reinstall or update the client.");
	CHECK(AP_SoloRuntimeProblemText(true, false, true) ==
	      "ctr_solo.py is missing from the solo-runtime folder. Reinstall or update the client.");
	CHECK(AP_SoloRuntimeProblemText(false, false, true) ==
	      "The solo-runtime folder is missing from the game folder. Reinstall or update the client.");
	// The runtime itself is checked first: with it gone, the apworld hint would mislead.
	CHECK(AP_SoloRuntimeProblemText(false, false, false) == AP_SoloRuntimeProblemText(false, false, true));
	CHECK(AP_SoloRuntimeProblemText(false, true, false) == AP_SoloRuntimeProblemText(false, true, true));

	// The wrapper's error after the job started (the early check missed it).
	CHECK(AP_SoloErrorText("runtime", "ctr.apworld is missing: C:\\Games\\CTR\\ctr.apworld") == apworld);
	CHECK(AP_SoloErrorText("runtime", "ctr.apworld is missing: /home/x/ctr.apworld") == apworld);
	// A different apworld problem keeps the damaged-runtime text.
	CHECK(AP_SoloErrorText("runtime", "ctr.apworld does not match the solo runtime") ==
	      "Solo runtime is damaged. Reinstall or update the client.");
	CHECK(AP_SoloErrorText("runtime", "") == "Solo runtime is damaged. Reinstall or update the client.");
	CHECK(AP_SoloErrorText("internal", "ctr.apworld is missing: x") ==
	      "Solo runtime is damaged. Reinstall or update the client.");

	if (g_fail)
		return 1;
	std::printf("ok\n");
	return 0;
}
