#!/usr/bin/env bash
# In-game credits completeness + glyph lint (issue #117).
#
# The AP credits screen is hand-authored (ap/ap_credits_data.h); this script is
# the machine check that keeps it honest:
#
#   1. Every SHIPPED component in THIRD_PARTY_NOTICES.md (a "## " / "### "
#      section whose body carries a License/Copyright notice and is not marked
#      "Not shipped") must be named in the credits data, so a newly vendored
#      dependency cannot ship uncredited.
#   2. A fixed set of human credits (design / ported-mod authors / maintainer)
#      must be present, so they cannot be dropped by accident.
#   3. Every credits text literal must fit AP_CREDITS_FIELD_WIDTH and use only
#      glyphs FONT_CREDITS can draw (see data.font_characterIconID): A-Z, a-z,
#      0-9, space, and  ! % ' , - . / : < = > ? _ +
#
# Usage: tools/check-credits.sh   (from anywhere; exits non-zero on failure)
set -u

root="$(cd "$(dirname "$0")/.." && pwd)"
notices="$root/THIRD_PARTY_NOTICES.md"
credits="$root/ap/ap_credits_data.h"
fail=0

[ -f "$notices" ] || { echo "FAIL: $notices not found"; exit 1; }
[ -f "$credits" ] || { echo "FAIL: $credits not found"; exit 1; }

# Uppercased credits data for case-insensitive matching.
credits_upper="$(tr '[:lower:]' '[:upper:]' < "$credits")"

# --- 1. Shipped THIRD_PARTY_NOTICES components ------------------------------
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

while IFS=$'\t' read -r heading shipped; do
	[ "$shipped" = "1" ] || continue

	# Candidate tokens: the heading's words (split on spaces and "/"), minus
	# parentheticals, uppercased. The component is credited when any token of
	# length >= 4 appears in the credits data.
	tokens="$(printf '%s' "$heading" | sed 's/([^)]*)//g' | tr '/' ' ' | tr '[:lower:]' '[:upper:]')"
	found=0
	for tok in $tokens; do
		[ "${#tok}" -ge 4 ] || continue
		case "$credits_upper" in
			*"$tok"*) found=1; break ;;
		esac
	done

	if [ "$found" = "0" ]; then
		echo "FAIL: THIRD_PARTY_NOTICES component not in credits data: $heading"
		fail=1
	fi
done <<EOF
$sections
EOF

# --- 2. Fixed human credits --------------------------------------------------
for name in DOWLLE ICEBOUND777 TAOR THECODINGBOB SUPERSTARXALIEN; do
	case "$credits_upper" in
		*"$name"*) : ;;
		*) echo "FAIL: required credit missing from credits data: $name"; fail=1 ;;
	esac
done

# --- 3. Glyph set + line width ----------------------------------------------
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
	echo "OK: credits data covers all shipped THIRD_PARTY_NOTICES components,"
	echo "    required human credits, and passes the glyph/width lint."
fi
exit "$fail"
