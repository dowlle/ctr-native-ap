// One-click-connect glue for the running client (issue #334, slice 3). See
// ap_link.h. Part of the AP unity build (game/game_unity.h), after ap_hooks.c,
// so it uses the feed and log helpers defined there.
//
// Diagnostics are fixed strings. No log line carries a request's host, slot,
// room, the ctr-ap URI or any password.

static NativeLinkHost ap_link_host;
static NativeLinkFsOps ap_link_ops;
static int ap_link_store;           // this process claims from the store
static NativeLinkHandoff ap_link;
static int ap_link_ready;           // store attached or a boot request waiting

static int ap_link_boot_pending;    // a command-line request not yet offered
static NativeLinkRecord ap_link_boot_record;

static struct RectMenu *ap_link_menu; // the settled top-level main menu
static int ap_link_menu_frame;        // sdata->frameCounter when last reported
static int ap_link_prompting;         // the prompt was on screen last frame
static int ap_link_poll_timer;

// A linked room that refused the (empty) password: say where to type it.
static int ap_link_watch_password;
static int ap_link_password_hint;

// Claim once a second while idle; more often costs two stat calls a frame.
#define AP_LINK_POLL_FRAMES 30

// Characters one prompt line can hold inside the panel.
#define AP_LINK_PROMPT_CHARS 30

NativeLinkHost *AP_LinkHost(void)
{
	return &ap_link_host;
}

static void AP_LinkEnsureInit(void)
{
	static int inited;
	if (!inited)
	{
		NativeLinkHandoff_Init(&ap_link);
		inited = 1;
	}
}

void AP_LinkAttach(int primary)
{
	AP_LinkEnsureInit();
	if (!primary)
		return;
	NativeLinkHost_Ops(&ap_link_host, &ap_link_ops);
	ap_link_store = 1;
	ap_link_ready = 1;
}

void AP_LinkSetBootRequest(const NativeLaunchRequest *request)
{
	AP_LinkEnsureInit();
	if (request == NULL)
		return;
	memset(&ap_link_boot_record, 0, sizeof ap_link_boot_record);
	ap_link_boot_record.request = *request;
	ap_link_boot_record.token = NativeLinkHost_NewToken();
	ap_link_boot_record.createdUnixMs = NativeLinkHost_NowUnixMs();
	ap_link_boot_pending = 1;
	ap_link_ready = 1;
}

static long long AP_LinkNowMono(void)
{
	return (long long)SDL_GetTicks();
}

static int AP_LinkSessionLive(void)
{
	const int s = ap_net_status();
	return s == AP_NET_STATUS_CONNECTED || s == AP_NET_STATUS_CONNECTING || s == AP_NET_STATUS_UNREACHABLE;
}

static void AP_LinkOffer(const NativeLinkRecord *record)
{
	switch (NativeLinkHandoff_Offer(&ap_link, record, NativeLinkHost_NowUnixMs(), AP_LinkNowMono(),
	                                AP_LinkSessionLive()))
	{
	case NATIVE_LINK_OFFER_EXPIRED:
		AP_AppendLog("[AP LINK] room link expired before it could be offered\n");
		break;
	case NATIVE_LINK_OFFER_COALESCED_ACTIVE:
		AP_AppendLog("[AP LINK] room link is the current session; kept as it is\n");
		break;
	case NATIVE_LINK_OFFER_COALESCED_WAITING:
		AP_AppendLog("[AP LINK] room link repeated; already waiting\n");
		break;
	case NATIVE_LINK_OFFER_WAITING:
		AP_AppendLog("[AP LINK] room link waiting for the main menu\n");
		break;
	case NATIVE_LINK_OFFER_REPLACED:
		AP_AppendLog("[AP LINK] newer room link replaced the waiting one\n");
		break;
	}
}

// Take whatever was published since the last poll, plus a command-line request.
static void AP_LinkPoll(void)
{
	NativeLinkRecord record;
	NativeLinkPendingStatus status;

	if (ap_link_boot_pending)
	{
		ap_link_boot_pending = 0;
		AP_LinkOffer(&ap_link_boot_record);
	}
	if (!ap_link_store)
		return;

	status = NativeLinkPending_Claim(&ap_link_ops, &record, NativeLinkHost_NowUnixMs());
	if (status == NATIVE_LINK_PENDING_CLAIMED)
	{
		AP_LinkOffer(&record);
	}
	else if (status != NATIVE_LINK_PENDING_NONE && status != NATIVE_LINK_PENDING_BUSY)
	{
		char msg[128];
		snprintf(msg, sizeof msg, "[AP LINK] %s\n", NativeLinkPending_StatusText(status));
		AP_AppendLog(msg);
	}
}

void AP_LinkNoteDial(const char *uri, const char *slot)
{
	NativeLaunchRequest identity;

	AP_LinkEnsureInit();
	// A dial of the linked session itself (same host, port and slot) keeps the
	// room it came with; anything else becomes the new session identity.
	if (NativeLinkIdentity_FromConnection(uri, slot, &identity))
	{
		if (!ap_link.haveActive || !NativeLinkRequest_SameIdentity(&ap_link.active, &identity))
			NativeLinkHandoff_SetActive(&ap_link, &identity);
	}
	else
	{
		NativeLinkHandoff_SetActive(&ap_link, NULL);
	}
}

// Save the accepted request as the connection and dial it.
static void AP_LinkApply(const NativeLinkRecord *record)
{
	char uri[sizeof g_config.uri];
	NativeLaunchRequest saved;
	int sameEndpoint = 0;

	if (!NativeLinkIdentity_ToConnectionUri(&record->request, uri, sizeof uri) ||
	    strlen(record->request.slot) >= sizeof g_config.slot)
	{
		AP_AppendLog("[AP LINK] room link does not fit the connection settings; ignored\n");
		return;
	}

	// The same host and port is the same room (one room listens per port), so
	// its saved password still applies. Any other room starts without one; a
	// passworded room then refuses and the player types it on the Connection
	// page. A password never travels in a link.
	if (NativeLinkIdentity_FromConnection(g_config.uri, g_config.slot, &saved))
	{
		snprintf(saved.slot, sizeof saved.slot, "%s", record->request.slot);
		saved.room[0] = '\0';
		sameEndpoint = NativeLinkRequest_SameIdentity(&saved, &record->request);
	}

	snprintf(g_config.uri, sizeof g_config.uri, "%s", uri);
	snprintf(g_config.slot, sizeof g_config.slot, "%s", record->request.slot);
	if (!sameEndpoint)
		g_config.password[0] = '\0';
	NativeConfig_Save();

	AP_AppendLog("[AP LINK] connecting to the room from the link\n");
	AP_Net_Reconnect(g_config.uri, g_config.slot, g_config.password);
	NativeLinkHandoff_SetActive(&ap_link, &record->request);
	ap_link_watch_password = (g_config.password[0] == '\0');
	ap_link_password_hint = 0;
}

int AP_LinkBootAdmit(void)
{
	NativeLinkFrameInput in;
	NativeLinkRecord taken;

	if (!ap_link_ready)
		return 0;
	AP_LinkPoll();
	if (!ap_link.haveOffer)
		return 0;

	// Before the first dial there is no session to switch away from.
	memset(&in, 0, sizeof in);
	in.safe = 1;
	in.sessionLive = 0;
	in.nowMonoMs = AP_LinkNowMono();
	if (NativeLinkHandoff_Frame(&ap_link, &in, &taken, NULL) != NATIVE_LINK_FRAME_ADMIT)
		return 0;
	AP_LinkApply(&taken);
	return 1;
}

void AP_LinkMainMenuSettled(struct RectMenu *menu)
{
	ap_link_menu = menu;
	ap_link_menu_frame = sdata->frameCounter;
}

// At the settled top-level main menu with nothing loading. While the prompt is
// up the menu proc does not run, so its report is not required then.
static int AP_LinkSafe(struct GameTracker *gGT)
{
	if (gGT == NULL || (gGT->gameMode1 & MAIN_MENU) == 0 || (gGT->gameMode1 & LOADING) != 0)
		return 0;
	if (gGT->levelID != MAIN_MENU_LEVEL || sdata->Loading.stage != LOAD_IDLE)
		return 0;
	if (ap_link_menu == NULL || sdata->ptrActiveMenu != ap_link_menu || sdata->ptrDesiredMenu != NULL)
		return 0;
	if ((ap_link_menu->state & DRAW_NEXT_MENU_IN_HIERARCHY) != 0)
		return 0;
	return ap_link_prompting || (sdata->frameCounter - ap_link_menu_frame) <= 1;
}

void AP_LinkTick(struct GameTracker *gGT)
{
	if (!ap_link_ready || gGT == NULL)
		return;

	if ((++ap_link_poll_timer % AP_LINK_POLL_FRAMES) == 0)
		AP_LinkPoll();

	// Off the main menu the menu hook does not run: expire here, and keep the
	// prompt disarmed so it re-arms when the menu is back.
	if ((gGT->gameMode1 & MAIN_MENU) == 0 && ap_link.haveOffer)
	{
		NativeLinkFrameInput in;
		memset(&in, 0, sizeof in);
		in.sessionLive = AP_LinkSessionLive();
		in.nowMonoMs = AP_LinkNowMono();
		if (NativeLinkHandoff_Frame(&ap_link, &in, NULL, NULL) == NATIVE_LINK_FRAME_EXPIRED)
			AP_AppendLog("[AP LINK] waiting room link expired\n");
		ap_link_prompting = 0;
	}

	if (((gGT->gameMode1 & MAIN_MENU) == 0 || gGT->levelID != MAIN_MENU_LEVEL) &&
	    NativeLinkHandoff_TakeNotice(&ap_link))
	{
		AP_FeedEnqueue("Room link waiting for the main menu", ORANGE, 0);
	}

	if (ap_link_watch_password)
	{
		const int s = ap_net_status();
		if (s == AP_NET_STATUS_CONNECTED)
			ap_link_watch_password = 0;
		else if (s == AP_NET_STATUS_ERROR)
		{
			const char *e = ap_net_last_error();
			ap_link_password_hint = (e != NULL && strstr(e, "InvalidPassword") != NULL);
			ap_link_watch_password = 0;
		}
	}
	else if (ap_link_password_hint && ap_net_status() == AP_NET_STATUS_CONNECTED)
	{
		ap_link_password_hint = 0;
	}
}

// Printable ASCII only (the menu font has no other glyphs); a value too long
// for the line is cut and marked with "..".
static void AP_LinkDisplayText(char *out, size_t cap, const char *prefix, const char *value, const char *suffix)
{
	const size_t fixed = strlen(prefix) + strlen(suffix);
	size_t room = (fixed < AP_LINK_PROMPT_CHARS) ? AP_LINK_PROMPT_CHARS - fixed : 0;
	size_t valueLen = strlen(value);
	size_t i;
	char cut[AP_LINK_PROMPT_CHARS + 1];

	if (valueLen > room)
		valueLen = (room >= 2) ? room - 2 : 0;
	for (i = 0; i < valueLen; i++)
	{
		const unsigned char c = (unsigned char)value[i];
		cut[i] = (c >= 0x20 && c < 0x7F) ? (char)c : '?';
	}
	cut[valueLen] = '\0';
	snprintf(out, cap, "%s%s%s%s", prefix, cut, (valueLen < strlen(value)) ? ".." : "", suffix);
}

// "Connect to <host>:<port>, slot <slot>?" (ruling 2026-09-22 23:24 CEST: the
// server address, not the room ID, which only matches and routes requests).
// Three lines, since one small-font line holds 30 characters; a long host is
// cut, the port never is.
static void AP_LinkDrawPrompt(const NativeLinkRecord *shown)
{
	struct GameTracker *gGT = sdata->gGT;
	uint32_t *ot = gGT->backBuffer->otMem.uiOT;
	char line[64];
	char port[12];
	RECT bg = {0x30, 0x46, 0x1A0, 0x64}; // 30 small characters (13 px) fit

	DecalFont_DrawLineOT("ROOM LINK", 0x100, 0x50, FONT_SMALL, JUSTIFY_CENTER | ORANGE, ot);
	DecalFont_DrawLineOT("Connect to", 0x100, 0x60, FONT_SMALL, JUSTIFY_CENTER | WHITE, ot);
	snprintf(port, sizeof port, ":%u,", shown->request.port);
	AP_LinkDisplayText(line, sizeof line, "", shown->request.host, port);
	DecalFont_DrawLineOT(line, 0x100, 0x6E, FONT_SMALL, JUSTIFY_CENTER | WHITE, ot);
	AP_LinkDisplayText(line, sizeof line, "slot ", shown->request.slot, "?");
	DecalFont_DrawLineOT(line, 0x100, 0x7C, FONT_SMALL, JUSTIFY_CENTER | WHITE, ot);
	DecalFont_DrawLineOT("*: CONNECT   ^: KEEP CURRENT", 0x100, 0x96, FONT_SMALL, JUSTIFY_CENTER | WHITE, ot);

	RECTMENU_DrawInnerRect(&bg, 4, ot);
}

int AP_LinkMenuFrame(void)
{
	struct GameTracker *gGT = sdata->gGT;
	NativeLinkFrameInput in;
	NativeLinkRecord taken;
	NativeLinkRecord shown;
	const int wasPrompting = ap_link_prompting;
	int tap;

	if (!ap_link_ready || gGT == NULL || (gGT->gameMode1 & MAIN_MENU) == 0 || !ap_link.haveOffer)
	{
		ap_link_prompting = 0;
		return 0;
	}

	tap = sdata->buttonTapPerPlayer[0];
	memset(&in, 0, sizeof in);
	in.safe = AP_LinkSafe(gGT);
	in.acceptTapped = (tap & BTN_CROSS) != 0;
	in.declineTapped = (tap & BTN_TRIANGLE) != 0;

	// A newer link replaces the one on screen: check before the prompt first
	// appears and before an answer counts. A replacement re-arms the prompt, so
	// this press cannot answer the new request.
	if (in.safe && (!wasPrompting || in.acceptTapped || in.declineTapped))
		AP_LinkPoll();
	if (!ap_link.haveOffer)
	{
		ap_link_prompting = 0;
		return wasPrompting;
	}

	in.sessionLive = AP_LinkSessionLive();
	in.nowMonoMs = AP_LinkNowMono();

	switch (NativeLinkHandoff_Frame(&ap_link, &in, &taken, &shown))
	{
	case NATIVE_LINK_FRAME_ADMIT:
		ap_link_prompting = 0;
		AP_LinkApply(&taken);
		return 0;
	case NATIVE_LINK_FRAME_ACCEPT:
		ap_link_prompting = 0;
		AP_AppendLog("[AP LINK] player accepted the room link\n");
		AP_LinkApply(&taken);
		return 1; // the accepting press must not also select a menu row
	case NATIVE_LINK_FRAME_DECLINED:
		ap_link_prompting = 0;
		AP_AppendLog("[AP LINK] player kept the current session\n");
		return 1;
	case NATIVE_LINK_FRAME_PROMPT:
		ap_link_prompting = 1;
		gGT->demoCountdownTimer = 900; // no attract demo behind an open question
		AP_LinkDrawPrompt(&shown);
		return 1;
	case NATIVE_LINK_FRAME_EXPIRED:
		ap_link_prompting = 0;
		AP_AppendLog("[AP LINK] waiting room link expired\n");
		return wasPrompting;
	case NATIVE_LINK_FRAME_IDLE:
	case NATIVE_LINK_FRAME_WAITING:
	default:
		ap_link_prompting = 0;
		return 0;
	}
}

void AP_LinkDrawTitleHint(uint32_t *ot)
{
	if (!ap_link_password_hint)
		return;
	DecalFont_DrawLineOT("Password needed: OPTIONS > Connection", 0x100, 0xC4, FONT_SMALL,
	                     JUSTIFY_CENTER | ORANGE, ot);
}

// ---------------------------------------------------------------------------
// ctr-ap:// link registration (issue #334, slice 4). Windows only for 0.2.1;
// elsewhere the Connection page shows no Room links row.

#if defined(_WIN32)
static NativeLinkRegOps ap_link_reg_ops;
static char ap_link_reg_exe[NATIVE_LINK_REG_TEXT_MAX];
static int ap_link_reg_ready;
static NativeLinkRegStatus ap_link_reg_status = NATIVE_LINK_REG_UNKNOWN;
static const char *ap_link_reg_message;     // result of the last action, this visit
static int ap_link_reg_page_frame = -1000;  // last frame the Connection page drew
static int ap_link_reg_notice;              // show the one-time notice this visit

#define AP_LINK_REG_NOTICE_FILE "room-link-notice.txt"

static int AP_LinkRegInit(void)
{
	if (!ap_link_reg_ready)
	{
		NativeLinkReg_WinOps(&ap_link_reg_ops);
		ap_link_reg_ready = NativeLinkReg_WinExePath(ap_link_reg_exe, sizeof ap_link_reg_exe) ? 1 : -1;
	}
	return ap_link_reg_ready == 1;
}

void AP_LinkRegisterAtLaunch(void)
{
	NativeLinkRegResult result;
	char msg[160];

	if (!AP_LinkRegInit())
	{
		AP_AppendLog("[AP LINK] room links: this client's path is unavailable; not registered\n");
		return;
	}
	result = NativeLinkReg_Register(&ap_link_reg_ops, ap_link_reg_exe, 0, &ap_link_reg_status);
	snprintf(msg, sizeof msg, "[AP LINK] room links at launch: %s (%s)\n", NativeLinkReg_ResultText(result),
	         NativeLinkReg_StatusText(ap_link_reg_status));
	AP_AppendLog(msg);
}

int AP_LinkRegRowAvailable(void)
{
	return 1;
}

static int AP_LinkRegNoticeSeen(void)
{
	char path[NATIVE_LINK_HOST_PATH_MAX + 32];
	snprintf(path, sizeof path, "%s/%s", ap_link_host.dir, AP_LINK_REG_NOTICE_FILE);
	return ap_link_host.dir[0] == '\0' || NativeFs_FileExists(path);
}

static void AP_LinkRegMarkNoticeSeen(void)
{
	char path[NATIVE_LINK_HOST_PATH_MAX + 32];
	FILE *f;
	if (ap_link_host.dir[0] == '\0')
		return;
	snprintf(path, sizeof path, "%s/%s", ap_link_host.dir, AP_LINK_REG_NOTICE_FILE);
	f = NativeFs_CreateExclusive(path);
	if (f != NULL)
		fclose(f);
}

void AP_LinkRegPageFrame(void)
{
	// A new visit re-reads the registry (another client may have taken room
	// links since) and decides once whether the one-time notice shows.
	if (sdata->frameCounter - ap_link_reg_page_frame > 1)
	{
		ap_link_reg_message = NULL;
		ap_link_reg_notice = 0;
		ap_link_reg_status = AP_LinkRegInit() ? NativeLinkReg_Status(&ap_link_reg_ops, ap_link_reg_exe)
		                                      : NATIVE_LINK_REG_UNKNOWN;
		if (ap_link_reg_status == NATIVE_LINK_REG_OTHER_PROGRAM && !AP_LinkRegNoticeSeen())
		{
			ap_link_reg_notice = 1;
			AP_LinkRegMarkNoticeSeen();
		}
	}
	ap_link_reg_page_frame = sdata->frameCounter;
}

const char *AP_LinkRegStatusText(void)
{
	return NativeLinkReg_StatusText(ap_link_reg_status);
}

const char *AP_LinkRegActionHint(void)
{
	if (ap_link_reg_message != NULL)
		return ap_link_reg_message;
	return (ap_link_reg_status == NATIVE_LINK_REG_THIS_CLIENT) ? "*: STOP USING THIS CLIENT FOR LINKS"
	                                                           : "*: USE THIS CLIENT FOR ROOM LINKS";
}

int AP_LinkRegNotice(const char **first, const char **second)
{
	if (!ap_link_reg_notice || ap_link_reg_status != NATIVE_LINK_REG_OTHER_PROGRAM)
		return 0;
	*first = "Another program opens room links.";
	*second = "Pick Room links to use this client.";
	return 1;
}

void AP_LinkRegAction(void)
{
	NativeLinkRegResult result;
	char msg[160];

	if (!AP_LinkRegInit())
	{
		ap_link_reg_message = "Room links unavailable here";
		return;
	}
	// The explicit action: the only path that replaces another program's
	// handler, and the only unregister.
	if (ap_link_reg_status == NATIVE_LINK_REG_THIS_CLIENT)
		result = NativeLinkReg_Unregister(&ap_link_reg_ops, ap_link_reg_exe, &ap_link_reg_status);
	else
		result = NativeLinkReg_Register(&ap_link_reg_ops, ap_link_reg_exe, 1, &ap_link_reg_status);
	ap_link_reg_notice = 0;
	ap_link_reg_message = (result == NATIVE_LINK_REG_OK) ? NULL : "Room links could not be changed";
	snprintf(msg, sizeof msg, "[AP LINK] room links action: %s (%s)\n", NativeLinkReg_ResultText(result),
	         NativeLinkReg_StatusText(ap_link_reg_status));
	AP_AppendLog(msg);
}
#else
void AP_LinkRegisterAtLaunch(void)
{
}

int AP_LinkRegRowAvailable(void)
{
	return 0;
}

void AP_LinkRegPageFrame(void)
{
}

const char *AP_LinkRegStatusText(void)
{
	return "";
}

const char *AP_LinkRegActionHint(void)
{
	return "";
}

int AP_LinkRegNotice(const char **first, const char **second)
{
	(void)first;
	(void)second;
	return 0;
}

void AP_LinkRegAction(void)
{
}
#endif
