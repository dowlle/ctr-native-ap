// cc -Wall -Wextra -DCTR_AP -I ap -I . -I include -o /tmp/test-author-gate tools/test-author-gate.c
//
// Box Author Mode in the player client (ap/ap_author_gate.h): when the mode may
// go on, the reason every dial shows while it is on, and the two file names
// that must never cross. The second half reads the production sources (run
// from the repo root, as tools/ci/run-harnesses.py does) and checks the wiring
// the rule depends on: author mode writes only the authoring files, the
// runtime never names the authoring file, every dial asks the gate, and the
// player client's config.ini never gains a box_author key.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ap_net.h"
#include "ap_author_gate.h"

static int checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)

static char *slurp(const char *path)
{
	FILE *f = fopen(path, "rb");
	long n;
	char *buf;

	if (f == NULL)
		return NULL;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	buf = (char *)malloc((size_t)n + 1);
	if (buf == NULL || fread(buf, 1, (size_t)n, f) != (size_t)n)
	{
		fclose(f);
		free(buf);
		return NULL;
	}
	buf[n] = '\0';
	fclose(f);
	return buf;
}

static int count(const char *hay, const char *needle)
{
	int n = 0;
	const char *p = hay;

	while (hay != NULL && (p = strstr(p, needle)) != NULL)
	{
		n++;
		p += strlen(needle);
	}
	return n;
}

int main(void)
{
	const char *a, *b;
	int st, solo;
	char *author, *boxes, *net, *menu, *config;

	// The status values this header repeats match ap_net.h.
	CHECK(AP_AUTHOR_GATE_NET_IDLE == AP_NET_STATUS_IDLE);
	CHECK(AP_AUTHOR_GATE_NET_CONNECTING == AP_NET_STATUS_CONNECTING);
	CHECK(AP_AUTHOR_GATE_NET_CONNECTED == AP_NET_STATUS_CONNECTED);
	CHECK(AP_AUTHOR_GATE_NET_ERROR == AP_NET_STATUS_ERROR);
	CHECK(AP_AUTHOR_GATE_NET_UNREACHABLE == AP_NET_STATUS_UNREACHABLE);
	CHECK(AP_AUTHOR_GATE_NET_RETRY_STOPPED == AP_NET_STATUS_RETRY_STOPPED);

	// Entering: refused while a room connects, is connected or is retried, and
	// while Play Solo runs; allowed with no client and after a refused or
	// stopped dial, which a closed client then leaves behind nothing of.
	for (st = AP_NET_STATUS_IDLE; st <= AP_NET_STATUS_RETRY_STOPPED; st++)
	{
		int room = st == AP_NET_STATUS_CONNECTING || st == AP_NET_STATUS_CONNECTED || st == AP_NET_STATUS_UNREACHABLE;
		CHECK(AP_AuthorGate_Enter(st, 0) == (room ? AP_AUTHOR_GATE_ROOM : AP_AUTHOR_GATE_OK));
		CHECK(AP_AuthorGate_Enter(st, 1) == AP_AUTHOR_GATE_SOLO);
		CHECK(AP_AuthorGate_CloseOnEnter(st) == (st == AP_NET_STATUS_ERROR || st == AP_NET_STATUS_RETRY_STOPPED));
		CHECK(!(room && AP_AuthorGate_CloseOnEnter(st))); // a live room is never closed silently
	}

	// Dialling: refused with a readable reason exactly while the mode is on.
	CHECK(AP_AuthorGate_DialRefusal(0) == NULL);
	CHECK(AP_AuthorGate_DialRefusal(1) != NULL);
	CHECK(strstr(AP_AuthorGate_DialRefusal(1), "Box Author Mode") != NULL);
	CHECK(strstr(AP_AuthorGate_DialRefusal(1), "Options > Authoring") != NULL);

	// Help lines fit the panel (33 characters at FONT_SMALL) in every state.
	for (st = 0; st < 2; st++)
	{
		for (solo = AP_AUTHOR_GATE_OK; solo <= AP_AUTHOR_GATE_SOLO; solo++)
		{
			a = b = NULL;
			AP_AuthorGate_Help(st, solo, &a, &b);
			CHECK(a != NULL && b != NULL && strlen(a) <= 33 && strlen(b) <= 33);
		}
	}

	// Files never cross: the player's file is read only while author mode does
	// not hold the table, and author mode only ever writes the authoring file.
	CHECK(strcmp(AP_PLACEMENT_FILE, "ap-box-placements.json") == 0);
	CHECK(strcmp(AP_AUTHOR_FILE, "ap-box-placements-authoring.json") == 0);
	CHECK(strcmp(AP_AuthorGate_ReadFile(0), AP_PLACEMENT_FILE) == 0);
	CHECK(strcmp(AP_AuthorGate_ReadFile(1), AP_AUTHOR_FILE) == 0);
	CHECK(strcmp(AP_AuthorGate_WriteFile(), AP_AUTHOR_FILE) == 0);
	CHECK(strcmp(AP_AuthorGate_WriteFile(), AP_PLACEMENT_FILE) != 0);

	// ── the production wiring ───────────────────────────────────────────────
	author = slurp("ap/ap_author.c");
	boxes = slurp("ap/ap_boxes.c");
	net = slurp("ap/ap_net.cpp");
	menu = slurp("game/230/MM_ConfigMenu.c");
	config = slurp("platform/native_config.c");
	CHECK(author != NULL && boxes != NULL && net != NULL && menu != NULL && config != NULL);
	if (author == NULL || boxes == NULL || net == NULL || menu == NULL || config == NULL)
	{
		printf("author gate: %d checks, %d failures (run from the repo root)\n", checks, failures);
		return 1;
	}

	// Every write in ap_author.c opens one of: the authoring table, the custom
	// authoring file, or an export path. None names the player's file.
	CHECK(count(author, "\"w\")") == 4);
	CHECK(count(author, "fopen(AP_AuthorGate_WriteFile(), \"w\")") == 1);
	CHECK(count(author, "fopen(AP_CUSTOM_BOX_FILE, \"w\")") == 1);
	CHECK(count(author, "fopen(path, \"w\")") == 2);
	CHECK(strstr(author, "AP_PLACEMENT_FILE, \"w\"") == NULL);
	CHECK(strstr(author, "\"ap-box-placements.json\"") == NULL);
	// The table is read through the gate, so the mode's own reads follow it.
	CHECK(strstr(author, "AP_AuthorGate_ReadFile(s_authorTable)") != NULL);
	// Export paths are built only from AP_BOX_EXPORT_DIR names.
	CHECK(count(author, "AP_BoxExport_RetailName(path") == 1);
	CHECK(count(author, "AP_BoxExport_CustomName(path") == 1);

	// The runtime boxes never name the authoring file.
	CHECK(strstr(boxes, "AP_AUTHOR_FILE") == NULL);
	CHECK(strstr(boxes, "placements-authoring") == NULL);

	// Every dial asks the gate before a client exists, and Play Solo asks it
	// before it opens.
	{
		const char *init = strstr(net, "extern \"C\" int ap_net_init(");
		const char *ask = init ? strstr(init, "ap_net_dial_blocked()") : NULL;
		const char *make = init ? strstr(init, "new APClient(") : NULL;
		CHECK(init != NULL && ask != NULL && make != NULL && ask < make);
	}
	CHECK(strstr(menu, "else if (!ap_net_dial_blocked())") != NULL);

	// The player client keeps box_author out of its config entries: the row
	// is a session switch, so config.ini is written exactly as before.
	{
		const char *row = strstr(config, "\"box_author\"");
		const char *guard = row;
		CHECK(row != NULL);
		while (guard != NULL && guard > config && strncmp(guard, "#ifdef CTR_BOX_AUTHORING", 24) != 0 &&
		       strncmp(guard, "#endif", 6) != 0)
			guard--;
		CHECK(guard != NULL && strncmp(guard, "#ifdef CTR_BOX_AUTHORING", 24) == 0);
		CHECK(count(config, "\"box_author\"") == 1);
	}

	free(author);
	free(boxes);
	free(net);
	free(menu);
	free(config);
	printf("author gate: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
