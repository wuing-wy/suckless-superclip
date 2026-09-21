#ifndef SUPERCLIP_TEXT_H
#define SUPERCLIP_TEXT_H

#include <stddef.h>

struct sc_text {
	char *data;
	size_t len;
	size_t cap;
	size_t cursor;
};

int sc_text_init(struct sc_text *text, size_t limit);
void sc_text_free(struct sc_text *text);
int sc_text_set(struct sc_text *text, const char *data, size_t len, size_t limit);
int sc_text_insert(struct sc_text *text, const char *data, size_t len, size_t limit);
void sc_text_left(struct sc_text *text);
void sc_text_right(struct sc_text *text);
void sc_text_home(struct sc_text *text);
void sc_text_end(struct sc_text *text);
void sc_text_backspace(struct sc_text *text);
void sc_text_delete(struct sc_text *text);

#endif
