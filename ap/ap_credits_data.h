#ifndef AP_CREDITS_DATA_H
#define AP_CREDITS_DATA_H

#ifdef CTR_AP

// Hand-authored AP credits section for the post-goal credits roll (issue #117).
//
// AP_Credits_PrependScroll (ap_hooks.c) renders this table into a "\r"-separated
// scroll block and prepends it to the vanilla relocated scroll, so the existing
// CS_Credits_DrawNames machinery (1px/frame scroll, 20px line pitch, fade,
// leading "~NN" colour codes) presents it unchanged.
//
// Content rules (checked by tools/check-credits.sh):
//   - FONT_CREDITS glyph set only: A-Z, 0-9, space, and  ! % ' , - . / : < = > ? _ +
//     Forbidden: ~ (colour-code escape), and the button-icon / unmapped glyphs
//     @ [ ] ^ * & ( ) # " ; $ { | } (see data.font_characterIconID).
//   - Max AP_CREDITS_FIELD_WIDTH chars per line (the scroller draws left-justified
//     at x=20 in a 14px/char font on the 512-wide UI; longer lines run off-screen).
//   - Handles only for people; library entries mirror THIRD_PARTY_NOTICES.md.
//     valijson is intentionally absent: AP_NO_SCHEMA compiles it out, nothing of
//     it ships in ctr_native_ap.
//
// Every shipped THIRD_PARTY_NOTICES.md component and the fixed human-credit set
// must appear here; tools/check-credits.sh enforces both.

// Max characters per credits line: draw origin x=20 (creditText_PosX), 14px per
// glyph slot, 512px UI width -> floor((512-20)/14) = 35.
#define AP_CREDITS_FIELD_WIDTH 35

struct ApCreditLine
{
	// 1 = section header (orange, colour slot 00), 0 = entry (white, slot 04).
	// Ignored for blank spacer lines (empty text).
	unsigned char header;
	const char *text;
};

// Order (per the #117 build brief): maintainer, randomizer design, decomp
// contributors, ported-mod authors, libraries.
static const struct ApCreditLine AP_CREDITS_LINES[] = {
    {1, "CRASH TEAM RACING ARCHIPELAGO"},
    {0, ""},
    {1, "CREATED BY"},
    {0, "APPIE/DOWLLE"},
    {0, ""},
    {1, "RANDOMIZER DESIGN"},
    {0, "APPIE/DOWLLE"},
    {0, ""},
    {1, "ORIGINAL RANDOMIZER DESIGN"},
    {0, "ICEBOUND777"},
    {0, "TAOR"},
    {0, ""},
    // Union of the CTR-tools/CTR-ModSDK and CTR-tools/ctr-native contributor
    // lists (verified identical via the GitHub API except RINNEGATAMANTE,
    // ctr-native only), by upstream contribution count.
    {1, "CTR-MODSDK DECOMPILATION"},
    {0, "AALHENDI"},
    {0, "SUPERSTARXALIEN"},
    {0, "MATEUSFAVARIN"},
    {0, "FRDS"},
    {0, "THEUBMUNSTER"},
    {0, "NIKO1POINT0"},
    {0, "DCXDEMO"},
    {0, "PEDROHLC"},
    {0, "CLAUDIOBO"},
    {0, "NONUNKNOWN777"},
    {0, "KKV0N"},
    {0, "CYBDYN-SYSTEMS"},
    {0, "RINNEGATAMANTE"},
    {0, "BEMUG"},
    {0, "BOMMEL24"},
    {0, "ICEBOUND777"},
    {0, "NOMADICS9"},
    {0, "MUFFINHOP"},
    {0, "TAZUA"},
    {0, ""},
    {1, "PORTED MODS"},
    {0, "OPTIONS MENU - THECODINGBOB"},
    {0, "RESERVES METER - SUPERSTARXALIEN"},
    {0, ""},
    {1, "LIBRARIES"},
    {0, "PSY-X - REDRIVER2 PROJECT"},
    {0, "PSN00BSDK - LAMEGUY64"},
    {0, "APCLIENTPP - BLACK-SLIVER"},
    {0, "AND FELICITUSNEKO"},
    {0, "WSWRAP - BLACK-SLIVER"},
    {0, "NLOHMANN JSON - NIELS LOHMANN"},
    {0, "WEBSOCKET++ - PETER THORSON"},
    {0, "ASIO - CHRIS KOHLHOFF"},
    {0, "SDL3 - SAM LANTINGA"},
    // Spacer gap between the AP section and the vanilla roll it precedes.
    {0, ""},
    {0, ""},
    {0, ""},
};

#define AP_CREDITS_NUM_LINES ((int)(sizeof(AP_CREDITS_LINES) / sizeof(AP_CREDITS_LINES[0])))

#endif // CTR_AP

#endif // AP_CREDITS_DATA_H
