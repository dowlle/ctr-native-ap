#include "platform/native_launch_request.h"

#include <string.h>

// One ctr-ap URI or one `connect <uri>` argument, parsed into a value with no
// side effects. See the header for the frozen contract; this file only
// implements it. Percent-decoding is manual on purpose: urlsplit/sscanf accept
// malformed escapes and out-of-range bytes that this contract must refuse.

static int hexDigitValue(unsigned char c)
{
	if (c >= '0' && c <= '9')
		return (int)(c - '0');
	if (c >= 'a' && c <= 'f')
		return (int)(c - 'a') + 10;
	if (c >= 'A' && c <= 'F')
		return (int)(c - 'A') + 10;
	return -1;
}

// Strict UTF-8: rejects overlong encodings, UTF-16 surrogates and anything
// above U+10FFFF. Control bytes are already refused by the decoder, but the
// validator still treats them as invalid input so it is safe on its own.
static int utf8Valid(const unsigned char *s, size_t n)
{
	size_t i = 0;

	while (i < n)
	{
		unsigned char c = s[i];

		if (c < 0x80)
		{
			i++;
			continue;
		}
		if (c >= 0xC2 && c <= 0xDF)
		{
			if (i + 1 >= n || s[i + 1] < 0x80 || s[i + 1] > 0xBF)
				return 0;
			i += 2;
			continue;
		}
		if (c == 0xE0)
		{
			if (i + 2 >= n || s[i + 1] < 0xA0 || s[i + 1] > 0xBF ||
			    s[i + 2] < 0x80 || s[i + 2] > 0xBF)
				return 0;
			i += 3;
			continue;
		}
		if (c >= 0xE1 && c <= 0xEC)
		{
			if (i + 2 >= n || s[i + 1] < 0x80 || s[i + 1] > 0xBF ||
			    s[i + 2] < 0x80 || s[i + 2] > 0xBF)
				return 0;
			i += 3;
			continue;
		}
		if (c == 0xED)
		{
			// Exclude U+D800..U+DFFF: the second byte must stay below 0xA0.
			if (i + 2 >= n || s[i + 1] < 0x80 || s[i + 1] > 0x9F ||
			    s[i + 2] < 0x80 || s[i + 2] > 0xBF)
				return 0;
			i += 3;
			continue;
		}
		if (c >= 0xEE && c <= 0xEF)
		{
			if (i + 2 >= n || s[i + 1] < 0x80 || s[i + 1] > 0xBF ||
			    s[i + 2] < 0x80 || s[i + 2] > 0xBF)
				return 0;
			i += 3;
			continue;
		}
		if (c == 0xF0)
		{
			if (i + 3 >= n || s[i + 1] < 0x90 || s[i + 1] > 0xBF ||
			    s[i + 2] < 0x80 || s[i + 2] > 0xBF ||
			    s[i + 3] < 0x80 || s[i + 3] > 0xBF)
				return 0;
			i += 4;
			continue;
		}
		if (c >= 0xF1 && c <= 0xF3)
		{
			if (i + 3 >= n || s[i + 1] < 0x80 || s[i + 1] > 0xBF ||
			    s[i + 2] < 0x80 || s[i + 2] > 0xBF ||
			    s[i + 3] < 0x80 || s[i + 3] > 0xBF)
				return 0;
			i += 4;
			continue;
		}
		if (c == 0xF4)
		{
			// Above U+10FFFF ends at 0x8F in the second byte.
			if (i + 3 >= n || s[i + 1] < 0x80 || s[i + 1] > 0x8F ||
			    s[i + 2] < 0x80 || s[i + 2] > 0xBF ||
			    s[i + 3] < 0x80 || s[i + 3] > 0xBF)
				return 0;
			i += 4;
			continue;
		}
		return 0;
	}
	return 1;
}

// Percent-decode one query component into dst (dstCap includes the NUL).
// Refuses malformed escapes, decoded controls and anything longer than the
// buffer's byte capacity. The decoded bytes are then UTF-8 validated.
static NativeLaunchRequestStatus decodeComponent(const char *s, size_t rawLen,
                                                  char *dst, size_t dstCap, size_t *outLen)
{
	size_t maxBytes = dstCap - 1;
	size_t n = 0;
	size_t i = 0;

	while (i < rawLen)
	{
		unsigned char c = (unsigned char)s[i];

		if (c == '%')
		{
			int hi;
			int lo;

			if (i + 2 >= rawLen)
				return NATIVE_LAUNCH_REQUEST_ERR_ESCAPE;
			hi = hexDigitValue((unsigned char)s[i + 1]);
			lo = hexDigitValue((unsigned char)s[i + 2]);
			if (hi < 0 || lo < 0)
				return NATIVE_LAUNCH_REQUEST_ERR_ESCAPE;
			c = (unsigned char)((hi << 4) | lo);
			i += 3;
		}
		else
		{
			i++;
		}

		if (c < 0x20 || c == 0x7F)
			return NATIVE_LAUNCH_REQUEST_ERR_CONTROL;
		if (n >= maxBytes)
			return NATIVE_LAUNCH_REQUEST_ERR_TOO_LONG;
		dst[n++] = (char)c;
	}

	dst[n] = '\0';
	if (!utf8Valid((const unsigned char *)dst, n))
		return NATIVE_LAUNCH_REQUEST_ERR_UTF8;
	if (outLen != NULL)
		*outLen = n;
	return NATIVE_LAUNCH_REQUEST_OK;
}

static int ipv4IsValid(const char *host)
{
	int parts = 0;
	int value = 0;
	int digits = 0;
	size_t i;

	for (i = 0; ; i++)
	{
		char c = host[i];

		if (c >= '0' && c <= '9')
		{
			value = value * 10 + (c - '0');
			digits++;
			if (digits > 3 || value > 255)
				return 0;
			continue;
		}
		if (c == '.' || c == '\0')
		{
			if (digits == 0)
				return 0;
			parts++;
			value = 0;
			digits = 0;
			if (c == '\0')
				break;
			continue;
		}
		return 0;
	}
	return parts == 4;
}

// DNS name, IPv4 literal or bracketed IPv6 literal. Whitespace, userinfo,
// slash, backslash, port colons and IPv6 zone identifiers are all outside the
// accepted character sets, so authority confusion fails here.
static int hostIsValid(const char *host)
{
	size_t n = strlen(host);
	size_t i;

	if (n == 0)
		return 0;

	if (host[0] == '[')
	{
		int colons = 0;

		if (n < 4 || host[n - 1] != ']')
			return 0;
		for (i = 1; i + 1 < n; i++)
		{
			unsigned char c = (unsigned char)host[i];

			if (c == ':')
			{
				colons++;
				continue;
			}
			if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
			    (c >= 'A' && c <= 'F') || c == '.')
				continue;
			return 0;
		}
		return colons >= 1;
	}

	{
		int onlyNumeric = 1;
		int labelLen = 0;

		for (i = 0; i < n; i++)
		{
			unsigned char c = (unsigned char)host[i];

			if (c == '.')
			{
				if (labelLen == 0)
					return 0;
				labelLen = 0;
				continue;
			}
			if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
			      (c >= '0' && c <= '9') || c == '-' || c == '_'))
				return 0;
			if (c < '0' || c > '9')
				onlyNumeric = 0;
			labelLen++;
		}
		if (labelLen == 0)
			return 0;
		if (onlyNumeric)
			return ipv4IsValid(host);
		return 1;
	}
}

static NativeLaunchRequestStatus parsePort(const char *s, unsigned int *out)
{
	size_t n = strlen(s);
	unsigned long value = 0;
	size_t i;

	if (n == 0 || n > 5)
		return NATIVE_LAUNCH_REQUEST_ERR_PORT;
	for (i = 0; i < n; i++)
	{
		if (s[i] < '0' || s[i] > '9')
			return NATIVE_LAUNCH_REQUEST_ERR_PORT;
		value = value * 10 + (unsigned long)(s[i] - '0');
	}
	if (value < NATIVE_LAUNCH_REQUEST_PORT_MIN || value > NATIVE_LAUNCH_REQUEST_PORT_MAX)
		return NATIVE_LAUNCH_REQUEST_ERR_PORT;
	*out = (unsigned int)value;
	return NATIVE_LAUNCH_REQUEST_OK;
}

static int hasForbiddenDelimiter(const char *s)
{
	for (; *s != '\0'; s++)
	{
		if (*s == '/' || *s == '\\' || *s == '@')
			return 1;
	}
	return 0;
}

static int authorityMatches(const char *rest, size_t headLen)
{
	const size_t authorityLen = sizeof(NATIVE_LAUNCH_REQUEST_AUTHORITY) - 1;

	if (headLen == authorityLen && memcmp(rest, NATIVE_LAUNCH_REQUEST_AUTHORITY, authorityLen) == 0)
		return 1;
	if (headLen == authorityLen + 1 && memcmp(rest, NATIVE_LAUNCH_REQUEST_AUTHORITY, authorityLen) == 0 &&
	    rest[authorityLen] == '/')
		return 1;
	return 0;
}

NativeLaunchRequestStatus NativeLaunchRequest_Parse(const char *uri, NativeLaunchRequest *out)
{
	const char *rest;
	const char *query;
	const char *p;
	size_t prefixLen;
	char key[16];
	char value[NATIVE_LAUNCH_REQUEST_HOST_MAX + 1];
	char host[NATIVE_LAUNCH_REQUEST_HOST_MAX + 1];
	char slot[NATIVE_LAUNCH_REQUEST_SLOT_MAX + 1];
	char room[NATIVE_LAUNCH_REQUEST_ROOM_MAX + 1];
	unsigned int port = 0;
	int haveHost = 0;
	int havePort = 0;
	int haveSlot = 0;
	int haveRoom = 0;

	if (uri == NULL || out == NULL)
		return NATIVE_LAUNCH_REQUEST_ERR_NULL;

	if (strlen(uri) == 0)
		return NATIVE_LAUNCH_REQUEST_ERR_EMPTY;
	if (strlen(uri) > NATIVE_LAUNCH_REQUEST_URI_MAX)
		return NATIVE_LAUNCH_REQUEST_ERR_TOO_LONG;

	prefixLen = sizeof(NATIVE_LAUNCH_REQUEST_URI_PREFIX) - 1;
	if (strncmp(uri, NATIVE_LAUNCH_REQUEST_URI_PREFIX, prefixLen) != 0)
		return NATIVE_LAUNCH_REQUEST_ERR_SCHEME;
	rest = uri + prefixLen;

	if (strchr(rest, '#') != NULL)
		return NATIVE_LAUNCH_REQUEST_ERR_FRAGMENT;

	query = strchr(rest, '?');
	if (query == NULL)
		return NATIVE_LAUNCH_REQUEST_ERR_QUERY;

	if (!authorityMatches(rest, (size_t)(query - rest)))
		return NATIVE_LAUNCH_REQUEST_ERR_AUTHORITY;

	p = query + 1;
	if (*p == '\0')
		return NATIVE_LAUNCH_REQUEST_ERR_MISSING_KEY;

	while (*p != '\0')
	{
		const char *amp = strchr(p, '&');
		const char *segEnd = (amp != NULL) ? amp : p + strlen(p);
		const char *eq = (const char *)memchr(p, '=', (size_t)(segEnd - p));
		size_t keyLen = 0;
		size_t valLen = 0;
		NativeLaunchRequestStatus status;

		if (eq == NULL || eq == p)
			return NATIVE_LAUNCH_REQUEST_ERR_QUERY;

		status = decodeComponent(p, (size_t)(eq - p), key, sizeof(key), &keyLen);
		if (status != NATIVE_LAUNCH_REQUEST_OK)
			return status;
		(void)keyLen;

		status = decodeComponent(eq + 1, (size_t)(segEnd - (eq + 1)), value, sizeof(value), &valLen);
		if (status != NATIVE_LAUNCH_REQUEST_OK)
			return status;

		if (strcmp(key, "host") == 0)
		{
			if (haveHost)
				return NATIVE_LAUNCH_REQUEST_ERR_DUPLICATE_KEY;
			if (valLen == 0)
				return NATIVE_LAUNCH_REQUEST_ERR_EMPTY;
			memcpy(host, value, valLen + 1);
			haveHost = 1;
		}
		else if (strcmp(key, "port") == 0)
		{
			if (havePort)
				return NATIVE_LAUNCH_REQUEST_ERR_DUPLICATE_KEY;
			status = parsePort(value, &port);
			if (status != NATIVE_LAUNCH_REQUEST_OK)
				return status;
			havePort = 1;
		}
		else if (strcmp(key, "slot") == 0)
		{
			if (haveSlot)
				return NATIVE_LAUNCH_REQUEST_ERR_DUPLICATE_KEY;
			if (valLen == 0)
				return NATIVE_LAUNCH_REQUEST_ERR_EMPTY;
			if (valLen > NATIVE_LAUNCH_REQUEST_SLOT_MAX)
				return NATIVE_LAUNCH_REQUEST_ERR_TOO_LONG;
			if (hasForbiddenDelimiter(value))
				return NATIVE_LAUNCH_REQUEST_ERR_SLOT;
			memcpy(slot, value, valLen + 1);
			haveSlot = 1;
		}
		else if (strcmp(key, "room") == 0)
		{
			if (haveRoom)
				return NATIVE_LAUNCH_REQUEST_ERR_DUPLICATE_KEY;
			if (valLen == 0)
				return NATIVE_LAUNCH_REQUEST_ERR_EMPTY;
			if (valLen > NATIVE_LAUNCH_REQUEST_ROOM_MAX)
				return NATIVE_LAUNCH_REQUEST_ERR_TOO_LONG;
			if (hasForbiddenDelimiter(value))
				return NATIVE_LAUNCH_REQUEST_ERR_ROOM;
			memcpy(room, value, valLen + 1);
			haveRoom = 1;
		}
		else
		{
			return NATIVE_LAUNCH_REQUEST_ERR_UNKNOWN_KEY;
		}

		if (amp == NULL)
			break;
		p = amp + 1;
		if (*p == '\0')
			return NATIVE_LAUNCH_REQUEST_ERR_QUERY;
	}

	if (!haveHost || !havePort || !haveSlot || !haveRoom)
		return NATIVE_LAUNCH_REQUEST_ERR_MISSING_KEY;

	if (!hostIsValid(host))
		return NATIVE_LAUNCH_REQUEST_ERR_HOST;

	// Everything validated: only now is the caller's request object touched,
	// so a rejected parse always leaves *out exactly as the caller set it.
	memcpy(out->host, host, strlen(host) + 1);
	out->port = port;
	memcpy(out->slot, slot, strlen(slot) + 1);
	memcpy(out->room, room, strlen(room) + 1);
	return NATIVE_LAUNCH_REQUEST_OK;
}

const char *NativeLaunchRequest_StatusText(NativeLaunchRequestStatus status)
{
	switch (status)
	{
	case NATIVE_LAUNCH_REQUEST_OK:
		return "accepted";
	case NATIVE_LAUNCH_REQUEST_ERR_NULL:
		return "no request";
	case NATIVE_LAUNCH_REQUEST_ERR_EMPTY:
		return "empty request or required field";
	case NATIVE_LAUNCH_REQUEST_ERR_TOO_LONG:
		return "request or field exceeds its byte limit";
	case NATIVE_LAUNCH_REQUEST_ERR_SCHEME:
		return "unexpected scheme";
	case NATIVE_LAUNCH_REQUEST_ERR_AUTHORITY:
		return "unexpected authority";
	case NATIVE_LAUNCH_REQUEST_ERR_FRAGMENT:
		return "fragment not allowed";
	case NATIVE_LAUNCH_REQUEST_ERR_QUERY:
		return "malformed query";
	case NATIVE_LAUNCH_REQUEST_ERR_ESCAPE:
		return "malformed percent escape";
	case NATIVE_LAUNCH_REQUEST_ERR_DUPLICATE_KEY:
		return "duplicate query key";
	case NATIVE_LAUNCH_REQUEST_ERR_UNKNOWN_KEY:
		return "unknown query key";
	case NATIVE_LAUNCH_REQUEST_ERR_MISSING_KEY:
		return "missing query key";
	case NATIVE_LAUNCH_REQUEST_ERR_CONTROL:
		return "control character not allowed";
	case NATIVE_LAUNCH_REQUEST_ERR_UTF8:
		return "invalid UTF-8";
	case NATIVE_LAUNCH_REQUEST_ERR_HOST:
		return "invalid host";
	case NATIVE_LAUNCH_REQUEST_ERR_PORT:
		return "invalid port";
	case NATIVE_LAUNCH_REQUEST_ERR_SLOT:
		return "invalid slot";
	case NATIVE_LAUNCH_REQUEST_ERR_ROOM:
		return "invalid room";
	default:
		return "rejected";
	}
}

NativeLaunchRequestArgForm NativeLaunchRequest_ClassifyArg(const char *arg)
{
	if (arg == NULL)
		return NATIVE_LAUNCH_REQUEST_ARG_NONE;
	if (strncmp(arg, NATIVE_LAUNCH_REQUEST_URI_PREFIX, sizeof(NATIVE_LAUNCH_REQUEST_URI_PREFIX) - 1) == 0)
		return NATIVE_LAUNCH_REQUEST_ARG_BARE;
	if (strcmp(arg, NATIVE_LAUNCH_REQUEST_AUTHORITY) == 0)
		return NATIVE_LAUNCH_REQUEST_ARG_CONNECT;
	return NATIVE_LAUNCH_REQUEST_ARG_NONE;
}
