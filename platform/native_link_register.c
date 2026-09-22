#include "platform/native_link_register.h"

#include <stdio.h>
#include <string.h>

// ctr-ap:// link registration decisions (issue #334, slice 4). See the header.
// Freestanding: no Windows headers, so the host harness compiles this unit on
// any platform against a simulated registry.

static int linkRegAsciiLower(int c)
{
	return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c;
}

// Windows paths compare case-insensitively; ASCII folding is enough to match
// the same executable path read back from the registry.
static int linkRegSameCommand(const char *a, const char *b)
{
	while (*a != '\0' && *b != '\0')
	{
		if (linkRegAsciiLower((unsigned char)*a) != linkRegAsciiLower((unsigned char)*b))
			return 0;
		a++;
		b++;
	}
	return *a == *b;
}

int NativeLinkReg_BuildCommand(const char *exe, char *out, size_t cap)
{
	const char *p;
	int n;

	if (exe == NULL || exe[0] == '\0' || out == NULL || cap == 0)
		return 0;
	for (p = exe; *p != '\0'; p++)
	{
		const unsigned char c = (unsigned char)*p;
		if (c < 0x20 || c == 0x7F || c == '"' || c == '%')
			return 0;
	}
	n = snprintf(out, cap, "\"%s\" \"%%1\"", exe);
	return n > 0 && (size_t)n < cap;
}

NativeLinkRegStatus NativeLinkReg_Classify(const NativeLinkRegState *state, const char *ourCommand)
{
	if (state == NULL)
		return NATIVE_LINK_REG_UNKNOWN;
	// A key without an open command and without our marker opens nothing.
	if (!state->present || (state->command[0] == '\0' && !state->owned))
		return NATIVE_LINK_REG_NONE;
	if (!state->owned)
		return NATIVE_LINK_REG_OTHER_PROGRAM;
	if (ourCommand != NULL && linkRegSameCommand(state->command, ourCommand))
		return NATIVE_LINK_REG_THIS_CLIENT;
	return NATIVE_LINK_REG_OTHER_CLIENT;
}

NativeLinkRegStatus NativeLinkReg_Status(const NativeLinkRegOps *ops, const char *exe)
{
	NativeLinkRegState state;
	char command[NATIVE_LINK_REG_TEXT_MAX];

	if (ops == NULL || !ops->read(ops->ctx, &state))
		return NATIVE_LINK_REG_UNKNOWN;
	if (!NativeLinkReg_BuildCommand(exe, command, sizeof command))
		command[0] = '\0';
	return NativeLinkReg_Classify(&state, command[0] != '\0' ? command : NULL);
}

static void linkRegDesired(NativeLinkRegState *s, const char *command)
{
	memset(s, 0, sizeof *s);
	s->present = 1;
	s->urlProtocol = 1;
	s->owned = 1;
	snprintf(s->description, sizeof s->description, "%s", NATIVE_LINK_REG_DESCRIPTION);
	snprintf(s->command, sizeof s->command, "%s", command);
}

// The key holds everything this client's handler needs.
static int linkRegComplete(const NativeLinkRegState *s, const char *command)
{
	return NativeLinkReg_Classify(s, command) == NATIVE_LINK_REG_THIS_CLIENT && s->urlProtocol &&
	       strcmp(s->description, NATIVE_LINK_REG_DESCRIPTION) == 0;
}

static NativeLinkRegResult linkRegRollback(const NativeLinkRegOps *ops, const NativeLinkRegState *before)
{
	NativeLinkRegState after;
	int ok;

	ok = before->present ? ops->write(ops->ctx, before) : ops->remove(ops->ctx);
	if (ok && ops->read(ops->ctx, &after) && after.present == before->present &&
	    (!before->present || (after.urlProtocol == before->urlProtocol && after.owned == before->owned &&
	                          strcmp(after.command, before->command) == 0 &&
	                          strcmp(after.description, before->description) == 0)))
		return NATIVE_LINK_REG_ROLLED_BACK;
	return NATIVE_LINK_REG_FAILED;
}

NativeLinkRegResult NativeLinkReg_Register(const NativeLinkRegOps *ops, const char *exe, int replaceOtherProgram,
                                           NativeLinkRegStatus *statusOut)
{
	char command[NATIVE_LINK_REG_TEXT_MAX];
	NativeLinkRegState before;
	NativeLinkRegState want;
	NativeLinkRegState after;
	NativeLinkRegStatus status;
	NativeLinkRegResult result;

	if (statusOut != NULL)
		*statusOut = NATIVE_LINK_REG_UNKNOWN;
	if (ops == NULL)
		return NATIVE_LINK_REG_FAILED;
	if (!NativeLinkReg_BuildCommand(exe, command, sizeof command))
	{
		if (statusOut != NULL)
			*statusOut = NativeLinkReg_Status(ops, NULL);
		return NATIVE_LINK_REG_BAD_PATH;
	}
	if (!ops->read(ops->ctx, &before))
		return NATIVE_LINK_REG_FAILED;

	status = NativeLinkReg_Classify(&before, command);
	if (statusOut != NULL)
		*statusOut = status;
	if (status == NATIVE_LINK_REG_OTHER_PROGRAM && !replaceOtherProgram)
		return NATIVE_LINK_REG_KEPT_OTHER;
	if (linkRegComplete(&before, command))
		return NATIVE_LINK_REG_OK;

	linkRegDesired(&want, command);
	if (ops->write(ops->ctx, &want) && ops->read(ops->ctx, &after) && linkRegComplete(&after, command))
	{
		if (statusOut != NULL)
			*statusOut = NATIVE_LINK_REG_THIS_CLIENT;
		return NATIVE_LINK_REG_OK;
	}

	result = linkRegRollback(ops, &before);
	if (statusOut != NULL)
		*statusOut = (result == NATIVE_LINK_REG_ROLLED_BACK) ? status : NativeLinkReg_Status(ops, exe);
	return result;
}

NativeLinkRegResult NativeLinkReg_Unregister(const NativeLinkRegOps *ops, const char *exe,
                                             NativeLinkRegStatus *statusOut)
{
	char command[NATIVE_LINK_REG_TEXT_MAX];
	NativeLinkRegState before;
	NativeLinkRegState after;
	NativeLinkRegStatus status;

	if (statusOut != NULL)
		*statusOut = NATIVE_LINK_REG_UNKNOWN;
	if (ops == NULL || !ops->read(ops->ctx, &before))
		return NATIVE_LINK_REG_FAILED;
	if (!NativeLinkReg_BuildCommand(exe, command, sizeof command))
		command[0] = '\0';

	status = NativeLinkReg_Classify(&before, command[0] != '\0' ? command : NULL);
	if (statusOut != NULL)
		*statusOut = status;
	if (status != NATIVE_LINK_REG_THIS_CLIENT)
		return NATIVE_LINK_REG_NOT_OURS;

	if (ops->remove(ops->ctx) && ops->read(ops->ctx, &after) && !after.present)
	{
		if (statusOut != NULL)
			*statusOut = NATIVE_LINK_REG_NONE;
		return NATIVE_LINK_REG_OK;
	}
	{
		const NativeLinkRegResult result = linkRegRollback(ops, &before);
		if (statusOut != NULL)
			*statusOut = NativeLinkReg_Status(ops, exe);
		return result;
	}
}

const char *NativeLinkReg_StatusText(NativeLinkRegStatus status)
{
	switch (status)
	{
	case NATIVE_LINK_REG_NONE:
		return "not set up";
	case NATIVE_LINK_REG_THIS_CLIENT:
		return "this client";
	case NATIVE_LINK_REG_OTHER_CLIENT:
		return "another CTR-AP client";
	case NATIVE_LINK_REG_OTHER_PROGRAM:
		return "another program";
	case NATIVE_LINK_REG_UNKNOWN:
	default:
		return "unavailable";
	}
}

const char *NativeLinkReg_ResultText(NativeLinkRegResult result)
{
	switch (result)
	{
	case NATIVE_LINK_REG_OK:
		return "ok";
	case NATIVE_LINK_REG_KEPT_OTHER:
		return "another program handles room links; left as it is";
	case NATIVE_LINK_REG_NOT_OURS:
		return "room links belong to another program or client; left as they are";
	case NATIVE_LINK_REG_BAD_PATH:
		return "this client's folder path cannot be registered for room links";
	case NATIVE_LINK_REG_ROLLED_BACK:
		return "room link registration failed and was undone";
	case NATIVE_LINK_REG_FAILED:
	default:
		return "room link registration failed";
	}
}
