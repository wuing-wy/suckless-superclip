#ifndef SUPERCLIP_UTIL_H
#define SUPERCLIP_UTIL_H

#include <stddef.h>

int sc_set_nonblock(int fd);
int sc_write_all(int fd, const void *buf, size_t len);
int sc_utf8_valid(const char *s, size_t len);
int sc_utf8_trim_whitespace(const char *s, size_t len, size_t *start, size_t *trimmed_len);
char *sc_xstrndup(const char *s, size_t len);
void sc_freep(void *p);

#endif
