#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CTR_CUSTOM_TRACKS 1
#include "platform/native_custom_content_verify.c"

static void json_string(const char *text)
{
	const unsigned char *p = (const unsigned char *)text;
	putchar('"');
	for (; *p; p++)
	{
		if (*p == '"' || *p == '\\') { putchar('\\'); putchar(*p); }
		else if (*p == '\n') fputs("\\n", stdout);
		else if (*p >= 0x20) putchar(*p);
	}
	putchar('"');
}

int main(int argc, char **argv)
{
	struct CustomContentVerification v;
	char error[256];
	int i;
	if (argc != 3)
	{
		fprintf(stderr, "usage: %s TRACK.lev TRACK.vrm\n", argv[0]);
		return 2;
	}
	if (!CustomContentVerify_Files(argv[1], argv[2], &v, error, sizeof error))
	{
		fprintf(stderr, "verification failed: %s\n", error);
		return 1;
	}
	printf("{\n  \"schema_version\": 1,\n  \"lev_sha256\": \"%s\",\n  \"vrm_sha256\": \"%s\",\n", v.levSha256, v.vrmSha256);
	printf("  \"lev_bytes\": %lu,\n  \"vrm_bytes\": %lu,\n", v.levBytes, v.vrmBytes);
	printf("  \"measurements\": {\"instances\": %lu, \"checkpoints\": %lu, \"spawns\": %lu, \"nav_paths\": %lu, \"ordinary_crates\": %lu, \"fruit_crates\": %lu, \"loose_wumpa\": %lu, \"relic_crates\": %lu, \"letter_c\": %lu, \"letter_t\": %lu, \"letter_r\": %lu, \"crystals\": %lu, \"start_lines\": %lu, \"finish_lines\": %lu},\n",
	       v.measured.instances, v.measured.checkpoints, v.measured.spawns,
	       v.measured.navPaths, v.measured.ordinaryCrates, v.measured.fruitCrates,
	       v.measured.looseWumpa, v.measured.relicCrates, v.measured.letterC,
	       v.measured.letterT, v.measured.letterR, v.measured.crystals,
	       v.measured.startLines, v.measured.finishLines);
	fputs("  \"file_analysis\": {\n", stdout);
	for (i = 0; i < CTR_CCV_MODE_COUNT; i++)
	{
		printf("    \"%s\": {\"result\": \"%s\", \"reason\": ",
		       CustomContentVerify_ModeSlug(i),
		       CustomContentVerify_EvidenceText(v.fileAnalysis[i].result));
		json_string(v.fileAnalysis[i].reason);
		printf("}%s\n", i + 1 == CTR_CCV_MODE_COUNT ? "" : ",");
	}
	fputs("  }\n}\n", stdout);
	return 0;
}
