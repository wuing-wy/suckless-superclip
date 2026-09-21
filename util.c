#include "util.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int
sc_set_nonblock(int fd)
{
	int flags;

	flags = fcntl(fd, F_GETFL);
	if (flags < 0)
		return -1;
	return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int
sc_write_all(int fd, const void *buf, size_t len)
{
	const unsigned char *p = buf;
	ssize_t n;

	while (len != 0) {
		n = write(fd, p, len);
		if (n > 0) {
			p += (size_t)n;
			len -= (size_t)n;
			continue;
		}
		if (n < 0 && errno == EINTR)
			continue;
		return -1;
	}
	return 0;
}

/* Rejects overlong encodings, surrogate code points, NUL and truncation. */
int
sc_utf8_valid(const char *s, size_t len)
{
	const unsigned char *p = (const unsigned char *)s;
	uint32_t cp;
	size_t i, left;

	if (s == NULL)
		return 0;
	for (i = 0; i < len;) {
		if (p[i] == 0)
			return 0;
		if (p[i] < 0x80) {
			i++;
			continue;
		}
		if ((p[i] & 0xe0U) == 0xc0U) {
			left = 1;
			cp = p[i] & 0x1fU;
			if (cp < 2)
				return 0;
		} else if ((p[i] & 0xf0U) == 0xe0U) {
			left = 2;
			cp = p[i] & 0x0fU;
		} else if ((p[i] & 0xf8U) == 0xf0U) {
			left = 3;
			cp = p[i] & 0x07U;
			if (cp > 4)
				return 0;
		} else {
			return 0;
		}
		if (left > len - i - 1)
			return 0;
		while (left-- != 0) {
			i++;
			if ((p[i] & 0xc0U) != 0x80U)
				return 0;
			cp = (cp << 6) | (p[i] & 0x3fU);
		}
		if (cp > 0x10ffffU || (cp >= 0xd800U && cp <= 0xdfffU))
			return 0;
		i++;
	}
	return 1;
}

static size_t
utf8_decode(const unsigned char *p, size_t len, uint32_t *codepoint)
{
	if (p == NULL || codepoint == NULL || len == 0)
		return 0;
	if (p[0] < 0x80U) {
		*codepoint = p[0];
		return 1;
	}
	if (len >= 2 && (p[0] & 0xe0U) == 0xc0U && (p[1] & 0xc0U) == 0x80U) {
		*codepoint = ((uint32_t)(p[0] & 0x1fU) << 6) | (p[1] & 0x3fU);
		return 2;
	}
	if (len >= 3 && (p[0] & 0xf0U) == 0xe0U && (p[1] & 0xc0U) == 0x80U &&
	    (p[2] & 0xc0U) == 0x80U) {
		*codepoint = ((uint32_t)(p[0] & 0x0fU) << 12) |
		    ((uint32_t)(p[1] & 0x3fU) << 6) | (p[2] & 0x3fU);
		return 3;
	}
	if (len >= 4 && (p[0] & 0xf8U) == 0xf0U && (p[1] & 0xc0U) == 0x80U &&
	    (p[2] & 0xc0U) == 0x80U && (p[3] & 0xc0U) == 0x80U) {
		*codepoint = ((uint32_t)(p[0] & 0x07U) << 18) |
		    ((uint32_t)(p[1] & 0x3fU) << 12) |
		    ((uint32_t)(p[2] & 0x3fU) << 6) | (p[3] & 0x3fU);
		return 4;
	}
	return 0;
}

static int
unicode_whitespace(uint32_t codepoint)
{
	return (codepoint >= 0x0009U && codepoint <= 0x000dU) || codepoint == 0x0020U ||
	    codepoint == 0x0085U || codepoint == 0x00a0U || codepoint == 0x1680U ||
	    (codepoint >= 0x2000U && codepoint <= 0x200aU) || codepoint == 0x2028U ||
	    codepoint == 0x2029U || codepoint == 0x202fU || codepoint == 0x205fU ||
	    codepoint == 0x3000U;
}

int
sc_utf8_trim_whitespace(const char *s, size_t len, size_t *start, size_t *trimmed_len)
{
	const unsigned char *p = (const unsigned char *)s;
	uint32_t codepoint;
	size_t offset, used, first, last;

	if (s == NULL || start == NULL || trimmed_len == NULL || !sc_utf8_valid(s, len))
		return -1;
	first = len;
	for (offset = 0; offset < len; offset += used) {
		used = utf8_decode(p + offset, len - offset, &codepoint);
		if (used == 0)
			return -1;
		if (!unicode_whitespace(codepoint)) {
			first = offset;
			break;
		}
	}
	if (first == len) {
		*start = len;
		*trimmed_len = 0;
		return 0;
	}
	last = first;
	for (offset = first; offset < len; offset += used) {
		used = utf8_decode(p + offset, len - offset, &codepoint);
		if (used == 0)
			return -1;
		if (!unicode_whitespace(codepoint))
			last = offset + used;
	}
	*start = first;
	*trimmed_len = last - first;
	return 0;
}

char *
sc_xstrndup(const char *s, size_t len)
{
	char *copy;

	if (len == SIZE_MAX)
		return NULL;
	copy = malloc(len + 1);
	if (copy == NULL)
		return NULL;
	memcpy(copy, s, len);
	copy[len] = '\0';
	return copy;
}

void
sc_freep(void *p)
{
	void **pp = p;

	if (pp != NULL) {
		free(*pp);
		*pp = NULL;
	}
}
