#include "platform/native_link_register.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ctr-ap:// link registration decisions (issue #334, slice 4). See the header.
// Freestanding: no Windows headers, so the host harness compiles this unit on
// any platform against a simulated registry.

static int linkRegAsciiLower(int c)
{
	return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c;
}

// Registry key and value names compare case-insensitively.
static int linkRegSameName(const char *a, const char *b)
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

// Nesting depth of a relative key path: 0 for the ctr-ap key itself.
static int linkRegDepth(const char *path)
{
	int depth = (path[0] != '\0') ? 1 : 0;
	for (; *path != '\0'; path++)
		if (*path == '\\')
			depth++;
	return depth;
}

// child is a direct subkey of parent.
static int linkRegIsChild(const char *child, const char *parent)
{
	const size_t n = strlen(parent);
	size_t i;

	if (linkRegDepth(child) != linkRegDepth(parent) + 1)
		return 0;
	if (n == 0)
		return 1;
	if (strlen(child) <= n || child[n] != '\\')
		return 0;
	for (i = 0; i < n; i++)
		if (linkRegAsciiLower((unsigned char)child[i]) != linkRegAsciiLower((unsigned char)parent[i]))
			return 0;
	return 1;
}

// ---------------------------------------------------------------------------
// Tree helpers.

void NativeLinkRegTree_Clear(NativeLinkRegTree *t)
{
	t->keyCount = 0;
	t->valueCount = 0;
	t->dataUsed = 0;
}

int NativeLinkRegTree_FindKey(const NativeLinkRegTree *t, const char *path)
{
	int i;
	for (i = 0; i < t->keyCount; i++)
		if (linkRegSameName(t->keys[i], path))
			return i;
	return -1;
}

int NativeLinkRegTree_AddKey(NativeLinkRegTree *t, const char *path)
{
	char parent[NATIVE_LINK_REG_PATH_MAX];
	const size_t len = strlen(path);
	size_t cut;
	int idx;

	if (len >= sizeof parent || linkRegDepth(path) > NATIVE_LINK_REG_DEPTH_MAX ||
	    (len > 0 && (path[0] == '\\' || path[len - 1] == '\\')))
		return -1;
	idx = NativeLinkRegTree_FindKey(t, path);
	if (idx >= 0)
		return idx;
	// The parent first (the ctr-ap key for a top-level subkey).
	if (len > 0)
	{
		for (cut = len; cut > 0 && path[cut - 1] != '\\'; cut--)
		{
		}
		cut = (cut > 0) ? cut - 1 : 0;
		memcpy(parent, path, cut);
		parent[cut] = '\0';
		if (NativeLinkRegTree_AddKey(t, parent) < 0)
			return -1;
	}
	if (t->keyCount >= NATIVE_LINK_REG_MAX_KEYS)
		return -1;
	memcpy(t->keys[t->keyCount], path, len + 1);
	return t->keyCount++;
}

static int linkRegFindValueAt(const NativeLinkRegTree *t, int key, const char *name)
{
	int i;
	for (i = 0; i < t->valueCount; i++)
		if (t->values[i].key == key && linkRegSameName(t->values[i].name, name))
			return i;
	return -1;
}

int NativeLinkRegTree_FindValue(const NativeLinkRegTree *t, const char *path, const char *name)
{
	const int key = NativeLinkRegTree_FindKey(t, path);
	return key < 0 ? -1 : linkRegFindValueAt(t, key, name);
}

int NativeLinkRegTree_SetValue(NativeLinkRegTree *t, const char *path, const char *name, unsigned long type,
                               const void *data, size_t size)
{
	const int key = NativeLinkRegTree_FindKey(t, path);
	int idx;
	NativeLinkRegValue *v;

	if (key < 0 || strlen(name) >= NATIVE_LINK_REG_NAME_MAX || size > NATIVE_LINK_REG_DATA_MAX - t->dataUsed)
		return 0;
	idx = linkRegFindValueAt(t, key, name);
	if (idx < 0)
	{
		if (t->valueCount >= NATIVE_LINK_REG_MAX_VALUES)
			return 0;
		idx = t->valueCount++;
		t->values[idx].key = key;
		snprintf(t->values[idx].name, sizeof t->values[idx].name, "%s", name);
	}
	// Data is appended; replaced data is not reclaimed. The limits cover every
	// tree this unit builds.
	v = &t->values[idx];
	v->type = type;
	v->offset = t->dataUsed;
	v->size = size;
	if (size > 0)
		memcpy(t->data + t->dataUsed, data, size);
	t->dataUsed += size;
	return 1;
}

int NativeLinkRegTree_DeleteValue(NativeLinkRegTree *t, const char *path, const char *name)
{
	const int idx = NativeLinkRegTree_FindValue(t, path, name);
	if (idx >= 0)
	{
		t->values[idx] = t->values[t->valueCount - 1];
		t->valueCount--;
	}
	return 1;
}

int NativeLinkRegTree_DeleteEmptyKey(NativeLinkRegTree *t, const char *path)
{
	const int key = NativeLinkRegTree_FindKey(t, path);
	int i;

	if (key < 0)
		return 1;
	for (i = 0; i < t->valueCount; i++)
		if (t->values[i].key == key)
			return 0;
	for (i = 0; i < t->keyCount; i++)
		if (linkRegIsChild(t->keys[i], t->keys[key]))
			return 0;
	// Values point at keys by index, so renumber after the removal.
	for (i = key; i < t->keyCount - 1; i++)
		memcpy(t->keys[i], t->keys[i + 1], sizeof t->keys[i]);
	t->keyCount--;
	for (i = 0; i < t->valueCount; i++)
		if (t->values[i].key > key)
			t->values[i].key--;
	return 1;
}

static int linkRegValueEqual(const NativeLinkRegTree *a, int ia, const NativeLinkRegTree *b, int ib)
{
	const NativeLinkRegValue *va = &a->values[ia];
	const NativeLinkRegValue *vb = &b->values[ib];
	return va->type == vb->type && va->size == vb->size &&
	       (va->size == 0 || memcmp(a->data + va->offset, b->data + vb->offset, va->size) == 0);
}

int NativeLinkRegTree_Equal(const NativeLinkRegTree *a, const NativeLinkRegTree *b)
{
	int i;

	if (a->keyCount != b->keyCount || a->valueCount != b->valueCount)
		return 0;
	for (i = 0; i < a->keyCount; i++)
		if (NativeLinkRegTree_FindKey(b, a->keys[i]) < 0)
			return 0;
	for (i = 0; i < a->valueCount; i++)
	{
		const int ib = NativeLinkRegTree_FindValue(b, a->keys[a->values[i].key], a->values[i].name);
		if (ib < 0 || !linkRegValueEqual(a, i, b, ib))
			return 0;
	}
	return 1;
}

// ---------------------------------------------------------------------------
// Text values. The registry stores REG_SZ as UTF-16LE with a terminating NUL.

// UTF-8 to UTF-16LE bytes including the terminator. Returns the byte count, or
// 0 for invalid UTF-8 or when it does not fit.
static size_t linkRegUtf16(const char *utf8, unsigned char *out, size_t cap)
{
	const unsigned char *p = (const unsigned char *)utf8;
	size_t n = 0;

	for (;;)
	{
		unsigned long cp;
		unsigned long min;
		int extra;

		if (*p < 0x80)
		{
			cp = *p;
			extra = 0;
			min = 0;
		}
		else if ((*p & 0xE0) == 0xC0)
		{
			cp = *p & 0x1F;
			extra = 1;
			min = 0x80;
		}
		else if ((*p & 0xF0) == 0xE0)
		{
			cp = *p & 0x0F;
			extra = 2;
			min = 0x800;
		}
		else if ((*p & 0xF8) == 0xF0)
		{
			cp = *p & 0x07;
			extra = 3;
			min = 0x10000;
		}
		else
			return 0;
		p++;
		for (; extra > 0; extra--, p++)
		{
			if ((*p & 0xC0) != 0x80)
				return 0;
			cp = (cp << 6) | (*p & 0x3F);
		}
		if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
			return 0;
		if (cp >= 0x10000)
		{
			const unsigned long hi = 0xD800 + ((cp - 0x10000) >> 10);
			const unsigned long lo = 0xDC00 + ((cp - 0x10000) & 0x3FF);
			if (n + 4 > cap)
				return 0;
			out[n++] = (unsigned char)(hi & 0xFF);
			out[n++] = (unsigned char)(hi >> 8);
			out[n++] = (unsigned char)(lo & 0xFF);
			out[n++] = (unsigned char)(lo >> 8);
		}
		else
		{
			if (n + 2 > cap)
				return 0;
			out[n++] = (unsigned char)(cp & 0xFF);
			out[n++] = (unsigned char)(cp >> 8);
		}
		if (cp == 0)
			return n;
	}
}

// The value is REG_SZ holding exactly this text and its terminator. foldCase
// compares ASCII letters case-insensitively (Windows paths).
static int linkRegValueIsText(const NativeLinkRegTree *t, const char *path, const char *name, const char *text,
                              int foldCase)
{
	unsigned char want[NATIVE_LINK_REG_TEXT_MAX * 2 + 2];
	const size_t n = linkRegUtf16(text, want, sizeof want);
	const int idx = NativeLinkRegTree_FindValue(t, path, name);
	const unsigned char *have;
	size_t i;

	if (n == 0 || idx < 0 || t->values[idx].type != NATIVE_LINK_REG_TYPE_SZ || t->values[idx].size != n)
		return 0;
	have = t->data + t->values[idx].offset;
	for (i = 0; i < n; i += 2)
	{
		const unsigned a = have[i] | ((unsigned)have[i + 1] << 8);
		const unsigned b = want[i] | ((unsigned)want[i + 1] << 8);
		if (a != b &&
		    !(foldCase && a < 0x80 && b < 0x80 && linkRegAsciiLower((int)a) == linkRegAsciiLower((int)b)))
			return 0;
	}
	return 1;
}

// The open command holds something: present and not an empty string.
static int linkRegHasCommand(const NativeLinkRegTree *t)
{
	const int idx = NativeLinkRegTree_FindValue(t, NATIVE_LINK_REG_COMMAND_KEY, "");
	const NativeLinkRegValue *v;

	if (idx < 0)
		return 0;
	v = &t->values[idx];
	if (v->type == NATIVE_LINK_REG_TYPE_SZ || v->type == NATIVE_LINK_REG_TYPE_EXPAND_SZ)
		return v->size >= 2 && (t->data[v->offset] != 0 || t->data[v->offset + 1] != 0);
	return 1;
}

// ---------------------------------------------------------------------------

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

NativeLinkRegStatus NativeLinkReg_Classify(const NativeLinkRegTree *tree, const char *ourCommand)
{
	int owned;

	if (tree == NULL)
		return NATIVE_LINK_REG_UNKNOWN;
	owned = NativeLinkRegTree_FindValue(tree, "", NATIVE_LINK_REG_OWNER) >= 0;
	// A key without an open command and without our marker opens nothing.
	if (tree->keyCount == 0 || (!linkRegHasCommand(tree) && !owned))
		return NATIVE_LINK_REG_NONE;
	if (!owned)
		return NATIVE_LINK_REG_OTHER_PROGRAM;
	if (ourCommand != NULL && linkRegValueIsText(tree, NATIVE_LINK_REG_COMMAND_KEY, "", ourCommand, 1))
		return NATIVE_LINK_REG_THIS_CLIENT;
	return NATIVE_LINK_REG_OTHER_CLIENT;
}

NativeLinkRegStatus NativeLinkReg_Status(const NativeLinkRegOps *ops, const char *exe)
{
	NativeLinkRegTree *tree;
	NativeLinkRegStatus status = NATIVE_LINK_REG_UNKNOWN;
	char command[NATIVE_LINK_REG_TEXT_MAX];

	if (ops == NULL || (tree = (NativeLinkRegTree *)malloc(sizeof *tree)) == NULL)
		return NATIVE_LINK_REG_UNKNOWN;
	NativeLinkRegTree_Clear(tree);
	if (ops->snapshot(ops->ctx, tree))
	{
		if (!NativeLinkReg_BuildCommand(exe, command, sizeof command))
			command[0] = '\0';
		status = NativeLinkReg_Classify(tree, command[0] != '\0' ? command : NULL);
	}
	free(tree);
	return status;
}

// The four values this client writes, in the order it writes them.
typedef struct
{
	const char *path;
	const char *name;
	const char *text;
} LinkRegOwnedValue;

static void linkRegOwnedValues(LinkRegOwnedValue out[4], const char *command)
{
	out[0].path = "";
	out[0].name = "";
	out[0].text = NATIVE_LINK_REG_DESCRIPTION;
	out[1].path = "";
	out[1].name = NATIVE_LINK_REG_URL_PROTOCOL;
	out[1].text = "";
	out[2].path = "";
	out[2].name = NATIVE_LINK_REG_OWNER;
	out[2].text = "1";
	out[3].path = NATIVE_LINK_REG_COMMAND_KEY;
	out[3].name = "";
	out[3].text = command;
}

// Every one of this client's values is there exactly as it writes them.
static int linkRegOwnedIntact(const NativeLinkRegTree *t, const char *command)
{
	LinkRegOwnedValue owned[4];
	int i;

	linkRegOwnedValues(owned, command);
	for (i = 0; i < 4; i++)
		if (!linkRegValueIsText(t, owned[i].path, owned[i].name, owned[i].text, i == 3))
			return 0;
	return 1;
}

// Scratch trees, on the heap: each is well over 64 KiB.
typedef struct
{
	NativeLinkRegTree before;
	NativeLinkRegTree expected;
	NativeLinkRegTree after;
} LinkRegWork;

// Undo everything since `before` was taken, then verify the whole tree
// against it. No recursive delete: keys that were not there before are
// emptied value by value and removed deepest first.
static NativeLinkRegResult linkRegRestore(const NativeLinkRegOps *ops, const NativeLinkRegTree *before,
                                          NativeLinkRegTree *scratch)
{
	int ok = 1;
	int depth;
	int i;

	NativeLinkRegTree_Clear(scratch);
	if (!ops->snapshot(ops->ctx, scratch))
		return NATIVE_LINK_REG_FAILED;

	// Keys that went missing, parents first.
	for (depth = 0; depth <= NATIVE_LINK_REG_DEPTH_MAX; depth++)
		for (i = 0; i < before->keyCount; i++)
			if (linkRegDepth(before->keys[i]) == depth && NativeLinkRegTree_FindKey(scratch, before->keys[i]) < 0)
				ok = ops->createKey(ops->ctx, before->keys[i]) && ok;
	// Values that changed or went missing, with their original type and data.
	for (i = 0; i < before->valueCount; i++)
	{
		const NativeLinkRegValue *v = &before->values[i];
		const char *path = before->keys[v->key];
		const int cur = NativeLinkRegTree_FindValue(scratch, path, v->name);
		if (cur < 0 || !linkRegValueEqual(before, i, scratch, cur))
			ok = ops->setValue(ops->ctx, path, v->name, v->type, before->data + v->offset, v->size) && ok;
	}
	// Values that were not there before.
	for (i = 0; i < scratch->valueCount; i++)
	{
		const NativeLinkRegValue *v = &scratch->values[i];
		if (NativeLinkRegTree_FindValue(before, scratch->keys[v->key], v->name) < 0)
			ok = ops->deleteValue(ops->ctx, scratch->keys[v->key], v->name) && ok;
	}
	// Keys that were not there before, deepest first.
	for (depth = NATIVE_LINK_REG_DEPTH_MAX; depth >= 0; depth--)
		for (i = 0; i < scratch->keyCount; i++)
			if (linkRegDepth(scratch->keys[i]) == depth && NativeLinkRegTree_FindKey(before, scratch->keys[i]) < 0)
				ok = ops->deleteEmptyKey(ops->ctx, scratch->keys[i]) && ok;

	NativeLinkRegTree_Clear(scratch);
	if (ok && ops->snapshot(ops->ctx, scratch) && NativeLinkRegTree_Equal(scratch, before))
		return NATIVE_LINK_REG_ROLLED_BACK;
	return NATIVE_LINK_REG_FAILED;
}

NativeLinkRegResult NativeLinkReg_Register(const NativeLinkRegOps *ops, const char *exe, int replaceOtherProgram,
                                           NativeLinkRegStatus *statusOut)
{
	char command[NATIVE_LINK_REG_TEXT_MAX];
	unsigned char text[NATIVE_LINK_REG_TEXT_MAX * 2 + 2];
	LinkRegOwnedValue owned[4];
	LinkRegWork *w;
	NativeLinkRegStatus status;
	NativeLinkRegResult result;
	int ok;
	int i;

	if (statusOut != NULL)
		*statusOut = NATIVE_LINK_REG_UNKNOWN;
	if (ops == NULL)
		return NATIVE_LINK_REG_FAILED;
	if (!NativeLinkReg_BuildCommand(exe, command, sizeof command) || linkRegUtf16(command, text, sizeof text) == 0)
	{
		if (statusOut != NULL)
			*statusOut = NativeLinkReg_Status(ops, NULL);
		return NATIVE_LINK_REG_BAD_PATH;
	}
	if ((w = (LinkRegWork *)malloc(sizeof *w)) == NULL)
		return NATIVE_LINK_REG_FAILED;
	NativeLinkRegTree_Clear(&w->before);
	if (!ops->snapshot(ops->ctx, &w->before))
	{
		free(w);
		return NATIVE_LINK_REG_FAILED;
	}

	status = NativeLinkReg_Classify(&w->before, command);
	if (statusOut != NULL)
		*statusOut = status;
	if (status == NATIVE_LINK_REG_OTHER_PROGRAM && !replaceOtherProgram)
	{
		free(w);
		return NATIVE_LINK_REG_KEPT_OTHER;
	}
	if (linkRegOwnedIntact(&w->before, command))
	{
		free(w);
		return NATIVE_LINK_REG_OK;
	}

	// Write this client's four values; everything else in the tree stays. The
	// expected tree is the snapshot with the same changes applied, and the
	// result must read back as exactly that.
	w->expected = w->before;
	linkRegOwnedValues(owned, command);
	ok = ops->createKey(ops->ctx, NATIVE_LINK_REG_COMMAND_KEY) &&
	     NativeLinkRegTree_AddKey(&w->expected, NATIVE_LINK_REG_COMMAND_KEY) >= 0;
	for (i = 0; ok && i < 4; i++)
	{
		const size_t n = linkRegUtf16(owned[i].text, text, sizeof text);
		ok = n > 0 && ops->setValue(ops->ctx, owned[i].path, owned[i].name, NATIVE_LINK_REG_TYPE_SZ, text, n) &&
		     NativeLinkRegTree_SetValue(&w->expected, owned[i].path, owned[i].name, NATIVE_LINK_REG_TYPE_SZ, text,
		                                n);
	}
	NativeLinkRegTree_Clear(&w->after);
	if (ok && ops->snapshot(ops->ctx, &w->after) && NativeLinkRegTree_Equal(&w->after, &w->expected))
	{
		free(w);
		if (statusOut != NULL)
			*statusOut = NATIVE_LINK_REG_THIS_CLIENT;
		return NATIVE_LINK_REG_OK;
	}

	result = linkRegRestore(ops, &w->before, &w->after);
	free(w);
	if (statusOut != NULL)
		*statusOut = (result == NATIVE_LINK_REG_ROLLED_BACK) ? status : NativeLinkReg_Status(ops, exe);
	return result;
}

NativeLinkRegResult NativeLinkReg_Unregister(const NativeLinkRegOps *ops, const char *exe,
                                             NativeLinkRegStatus *statusOut)
{
	// The keys this client creates, deepest first.
	static const char *const chain[] = {NATIVE_LINK_REG_COMMAND_KEY, "shell\\open", "shell", ""};
	char command[NATIVE_LINK_REG_TEXT_MAX];
	LinkRegOwnedValue owned[4];
	LinkRegWork *w;
	NativeLinkRegStatus status;
	NativeLinkRegResult result;
	int ok = 1;
	int i;

	if (statusOut != NULL)
		*statusOut = NATIVE_LINK_REG_UNKNOWN;
	if (ops == NULL || (w = (LinkRegWork *)malloc(sizeof *w)) == NULL)
		return NATIVE_LINK_REG_FAILED;
	NativeLinkRegTree_Clear(&w->before);
	if (!ops->snapshot(ops->ctx, &w->before))
	{
		free(w);
		return NATIVE_LINK_REG_FAILED;
	}
	if (!NativeLinkReg_BuildCommand(exe, command, sizeof command))
		command[0] = '\0';

	status = NativeLinkReg_Classify(&w->before, command[0] != '\0' ? command : NULL);
	if (statusOut != NULL)
		*statusOut = status;
	if (status != NATIVE_LINK_REG_THIS_CLIENT)
	{
		free(w);
		return NATIVE_LINK_REG_NOT_OURS;
	}
	// Something else changed one of this client's values: change nothing.
	if (!linkRegOwnedIntact(&w->before, command))
	{
		free(w);
		return NATIVE_LINK_REG_CHANGED;
	}

	// Remove this client's four values, then each key on the command path
	// that is left empty, deepest first. A key still holding anything stays.
	w->expected = w->before;
	linkRegOwnedValues(owned, command);
	for (i = 0; ok && i < 4; i++)
	{
		NativeLinkRegTree_DeleteValue(&w->expected, owned[i].path, owned[i].name);
		ok = ops->deleteValue(ops->ctx, owned[i].path, owned[i].name);
	}
	for (i = 0; ok && i < 4; i++)
		if (NativeLinkRegTree_DeleteEmptyKey(&w->expected, chain[i]))
			ok = ops->deleteEmptyKey(ops->ctx, chain[i]);
	NativeLinkRegTree_Clear(&w->after);
	if (ok && ops->snapshot(ops->ctx, &w->after) && NativeLinkRegTree_Equal(&w->after, &w->expected))
	{
		if (statusOut != NULL)
			*statusOut = NativeLinkReg_Classify(&w->after, command);
		free(w);
		return NATIVE_LINK_REG_OK;
	}

	result = linkRegRestore(ops, &w->before, &w->after);
	free(w);
	if (statusOut != NULL)
		*statusOut = NativeLinkReg_Status(ops, exe);
	return result;
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
	case NATIVE_LINK_REG_CHANGED:
		return "the room link handler was changed outside this client; left as it is";
	case NATIVE_LINK_REG_FAILED:
	default:
		return "room link registration failed";
	}
}
