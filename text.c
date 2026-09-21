#include "text.h"
#include "util.h"

#include <grapheme.h>

#include <stdlib.h>
#include <string.h>

static size_t
next_break(const struct sc_text *text, size_t offset)
{
	size_t n;

	if (offset >= text->len)
		return text->len;
	n = grapheme_next_character_break_utf8(text->data + offset, text->len - offset);
	if (n == 0 || n > text->len - offset)
		return text->len;
	return offset + n;
}

static size_t
prev_break(const struct sc_text *text, size_t offset)
{
	size_t pos = 0, next;

	while (pos < offset) {
		next = next_break(text, pos);
		if (next >= offset)
			return pos;
		pos = next;
	}
	return 0;
}

static int
reserve(struct sc_text *text, size_t need, size_t limit)
{
	char *next;
	size_t cap;

	if (need > limit || need == (size_t)-1)
		return -1;
	if (need + 1 <= text->cap)
		return 0;
	cap = text->cap == 0 ? 32 : text->cap;
	while (cap < need + 1) {
		if (cap > limit / 2) {
			cap = limit + 1;
			break;
		}
		cap *= 2;
	}
	next = realloc(text->data, cap);
	if (next == NULL)
		return -1;
	text->data = next;
	text->cap = cap;
	return 0;
}

int
sc_text_init(struct sc_text *text, size_t limit)
{
	if (text == NULL || limit == 0)
		return -1;
	memset(text, 0, sizeof(*text));
	return reserve(text, 0, limit);
}

void
sc_text_free(struct sc_text *text)
{
	if (text == NULL)
		return;
	free(text->data);
	memset(text, 0, sizeof(*text));
}

int
sc_text_set(struct sc_text *text, const char *data, size_t len, size_t limit)
{
	if (text == NULL || (data == NULL && len != 0) || !sc_utf8_valid(data, len))
		return -1;
	if (reserve(text, len, limit) < 0)
		return -1;
	memcpy(text->data, data, len);
	text->data[len] = '\0';
	text->len = len;
	text->cursor = len;
	return 0;
}

int
sc_text_insert(struct sc_text *text, const char *data, size_t len, size_t limit)
{
	if (text == NULL || (data == NULL && len != 0) || !sc_utf8_valid(data, len) || len > limit - text->len)
		return -1;
	if (reserve(text, text->len + len, limit) < 0)
		return -1;
	memmove(text->data + text->cursor + len, text->data + text->cursor, text->len - text->cursor + 1);
	memcpy(text->data + text->cursor, data, len);
	text->cursor += len;
	text->len += len;
	return 0;
}

void
sc_text_left(struct sc_text *text)
{
	if (text != NULL)
		text->cursor = prev_break(text, text->cursor);
}

void
sc_text_right(struct sc_text *text)
{
	if (text != NULL)
		text->cursor = next_break(text, text->cursor);
}

void
sc_text_home(struct sc_text *text)
{
	if (text != NULL)
		text->cursor = 0;
}

void
sc_text_end(struct sc_text *text)
{
	if (text != NULL)
		text->cursor = text->len;
}

void
sc_text_backspace(struct sc_text *text)
{
	size_t start;

	if (text == NULL || text->cursor == 0)
		return;
	start = prev_break(text, text->cursor);
	memmove(text->data + start, text->data + text->cursor, text->len - text->cursor + 1);
	text->len -= text->cursor - start;
	text->cursor = start;
}

void
sc_text_delete(struct sc_text *text)
{
	size_t end;

	if (text == NULL || text->cursor == text->len)
		return;
	end = next_break(text, text->cursor);
	memmove(text->data + text->cursor, text->data + end, text->len - end + 1);
	text->len -= end - text->cursor;
}
