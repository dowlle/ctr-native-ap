#!/usr/bin/env bash
# In-game credits completeness + glyph lint (issues #117, #231).
#
# The AP credits screen is hand-authored (ap/ap_credits_data.h); this script is
# the machine check that keeps it honest:
#
#   1. Every SHIPPED component in THIRD_PARTY_NOTICES.md (a "## " / "### "
#      section whose body carries a License/Copyright notice and is not marked
#      "Not shipped") has an entry in EXPECTED_CREDITS below naming the credits
#      that section owes, and every one of those names is present in the credits
#      data. A newly vendored dependency therefore cannot ship uncredited: it
#      has no entry, and a missing entry is itself a failure.
#   2. A fixed set of human credits (design / ported-mod authors / maintainer)
#      must be present, so they cannot be dropped by accident.
#   3. Credit lines whose subject a bare name cannot express (the PORTED MODS
#      block credits one author for two separate ports) must be present verbatim.
#   4. Every credits text literal must fit AP_CREDITS_FIELD_WIDTH and use only
#      glyphs FONT_CREDITS can draw (see data.font_characterIconID): A-Z, a-z,
#      0-9, space, and  ! % ' , - . / : < = > ? _ +
#
# Matching rule (#231): names are matched EXACTLY, as whole tokens in the credits
# text literals -- the name must appear with a non-alphanumeric character (or a
# line edge) on both sides. The check that shipped with #117 instead split the
# notices heading into words and passed a section when any word of four or more
# characters appeared anywhere in the credits data, so "Archipelago Logo Marker
# (AP build only)" was passed by the token ARCHIPELAGO matching the title line
# while none of its authors were named. Nothing here reads heading words any
# more, so no section can be waved through by a word it happens to share with an
# unrelated credit line.
#
# Maintaining this file: when you vendor a component or ship a new third-party
# asset, add its THIRD_PARTY_NOTICES.md heading and the names it owes to
# EXPECTED_CREDITS, and add those names to ap/ap_credits_data.h. When a component
# stops shipping, remove both.
#
# Usage: tools/check-credits.sh   (from anywhere; exits non-zero on failure)

# -f: credit names are split on ';' below and must never be glob-expanded.
set -uf

root="$(cd "$(dirname "$0")/.." && pwd)"
notices="$root/THIRD_PARTY_NOTICES.md"
credits="$root/ap/ap_credits_data.h"
fail=0

[ -f "$notices" ] || { echo "FAIL: $notices not found"; exit 1; }
[ -f "$credits" ] || { echo "FAIL: $credits not found"; exit 1; }

# The credits text literals, one per line, uppercased. Text lives only in the
# {header, "TEXT"} table rows.
credit_lines="$(grep -o '{[01], "[^"]*"' "$credits" \
	| sed 's/^{[01], "//; s/"$//' \
	| tr '[:lower:]' '[:upper:]')"

# name_present NAME -- true when NAME appears in a credits line as a whole token
# run, i.e. bounded by a non-alphanumeric character or a line edge on both sides.
# Uses index() rather than a regex so names containing + - / . are matched
# literally.
name_present() {
	printf '%s\n' "$credit_lines" | awk -v name="$1" '
		{
			start = 1
			while ((pos = index(substr($0, start), name)) > 0) {
				pos += start - 1
				before = (pos == 1) ? "" : substr($0, pos - 1, 1)
				after  = substr($0, pos + length(name), 1)
				if (before !~ /[A-Z0-9]/ && after !~ /[A-Z0-9]/) {
					found = 1
					exit
				}
				start = pos + 1
			}
		}
		END { exit found ? 0 : 1 }
	'
}

# --- 1. Shipped THIRD_PARTY_NOTICES components ------------------------------
#
# heading<TAB>NAME;NAME;...  -- the attributable names each shipped section owes
# on the credits screen. Keys are the exact THIRD_PARTY_NOTICES.md heading text
# (without the leading #s). Sections marked "Not shipped" (valijson) and
# container headings that carry no license of their own (the Archipelago
# Networking Dependencies umbrella) are deliberately absent.
EXPECTED_CREDITS="$(cat <<'EOF'
PsyCross / Psy-X	PSY-X;REDRIVER2 PROJECT
PSn00bSDK	PSN00BSDK;LAMEGUY64
apclientpp	APCLIENTPP;BLACK-SLIVER;FELICITUSNEKO
wswrap	WSWRAP;BLACK-SLIVER
nlohmann/json	NLOHMANN JSON;NIELS LOHMANN
WebSocket++	WEBSOCKET++;PETER THORSON
Asio	ASIO;CHRIS KOHLHOFF
Archipelago Logo Marker (AP build only)	MMRECOMPRANDO;KRISTA CORKOS;CHRISTOPHER WILSON
SDL3	SDL3;SAM LANTINGA
EOF
)"

# Emit "heading<TAB>shipped(0/1)" per section: split on ## / ### headings; a
# section is shipped when its own body (up to the next heading) contains a
# License or Copyright line and no "Not shipped" marker.
sections="$(awk '
	/^##+ / {
		if (heading != "") printf "%s\t%d\n", heading, (lic && !notshipped) ? 1 : 0
		heading = $0; sub(/^#+ /, "", heading)
		lic = 0; notshipped = 0; next
	}
	/License|Copyright/  { lic = 1 }
	/Not shipped/        { notshipped = 1 }
	END {
		if (heading != "") printf "%s\t%d\n", heading, (lic && !notshipped) ? 1 : 0
	}
' "$notices")"

shipped_headings=""
while IFS=$'\t' read -r heading shipped; do
	[ "$shipped" = "1" ] || continue
	shipped_headings="$shipped_headings$heading
"

	expected="$(printf '%s\n' "$EXPECTED_CREDITS" \
		| awk -F'\t' -v h="$heading" '$1 == h { print $2; found = 1 } END { exit found ? 0 : 1 }')"
	if [ $? -ne 0 ]; then
		echo "FAIL: shipped THIRD_PARTY_NOTICES component has no expected-credit list in tools/check-credits.sh: $heading"
		fail=1
		continue
	fi

	outer_ifs="$IFS"
	IFS=';'
	for name in $expected; do
		IFS="$outer_ifs"
		if ! name_present "$name"; then
			echo "FAIL: credit missing from credits data: $name (owed by THIRD_PARTY_NOTICES section: $heading)"
			fail=1
		fi
		IFS=';'
	done
	IFS="$outer_ifs"
done <<EOF
$sections
EOF

# The table must not outlive the notices either: an entry for a section that no
# longer ships is stale and would quietly stop protecting anything.
while IFS=$'\t' read -r heading _rest; do
	[ -n "$heading" ] || continue
	case "$shipped_headings" in
		*"$heading
"*) : ;;
		*)
			echo "FAIL: expected-credit list names a section that is not a shipped THIRD_PARTY_NOTICES component: $heading"
			fail=1
			;;
	esac
done <<EOF
$EXPECTED_CREDITS
EOF

# --- 2. Fixed human credits --------------------------------------------------
for name in DOWLLE ICEBOUND777 TAOR THECODINGBOB SUPERSTARXALIEN; do
	if ! name_present "$name"; then
		echo "FAIL: required credit missing from credits data: $name"
		fail=1
	fi
done

# --- 3. Verbatim credit lines ------------------------------------------------
# One line per ported mod. A name check cannot cover these: THECODINGBOB is
# credited twice, for two separate ports, so the name alone stays satisfied when
# either of its lines is deleted.
while IFS= read -r required; do
	[ -n "$required" ] || continue
	case "
$credit_lines
" in
		*"
$required
"*) : ;;
		*)
			echo "FAIL: required credit line missing from credits data: $required"
			fail=1
			;;
	esac
done <<'EOF'
OPTIONS MENU - THECODINGBOB
GRAPHICS OPTIONS - THECODINGBOB
RESERVES METER - SUPERSTARXALIEN
EOF

# --- 4. Glyph set + line width ----------------------------------------------
width="$(sed -n 's/^#define AP_CREDITS_FIELD_WIDTH \([0-9]*\).*/\1/p' "$credits")"
[ -n "$width" ] || { echo "FAIL: AP_CREDITS_FIELD_WIDTH not found in $credits"; exit 1; }

# Text literals live only in the {header, "TEXT"} table rows.
glyph_fail="$(grep -o '{[01], "[^"]*"' "$credits" | sed 's/^{[01], "//; s/"$//' | awk -v w="$width" '
	{
		if (length($0) > w) {
			printf "FAIL: credits line longer than %d chars: %s\n", w, $0
		}
		n = split($0, ch, "")
		for (i = 1; i <= n; i++) {
			if (ch[i] !~ /[A-Za-z0-9 !%'"'"',\-.\/:<=>?_+]/) {
				printf "FAIL: glyph FONT_CREDITS cannot draw (%s) in line: %s\n", ch[i], $0
			}
		}
	}
')"
if [ -n "$glyph_fail" ]; then
	echo "$glyph_fail"
	fail=1
fi

if [ "$fail" = "0" ]; then
	echo "OK: credits data names every credit owed by a shipped THIRD_PARTY_NOTICES"
	echo "    component, carries the required human and ported-mod credits, and"
	echo "    passes the glyph/width lint."
fi
exit "$fail"
