#ifndef SUPERCLIP_CLIPBOARD_STORE_H
#define SUPERCLIP_CLIPBOARD_STORE_H

#include <stddef.h>
#include <stdint.h>

#define SC_CLIP_MAX_ENTRY (256U * 1024U)
#define SC_CLIP_MAX_ENTRIES 200U
#define SC_CLIP_MAX_STATE (8U * 1024U * 1024U)

struct sc_clip_entry {
	uint64_t id;
	char *text;
	size_t len;
};

struct sc_clip_store {
	struct sc_clip_entry entries[SC_CLIP_MAX_ENTRIES];
	size_t len;
	uint64_t next_id;
	char *path;
};

int sc_clip_store_open(struct sc_clip_store *store, const char *path);
void sc_clip_store_close(struct sc_clip_store *store);
int sc_clip_store_add(struct sc_clip_store *store, const char *text, size_t len);
int sc_clip_store_touch(struct sc_clip_store *store, uint64_t id);
const struct sc_clip_entry *sc_clip_store_find(const struct sc_clip_store *store, uint64_t id);
int sc_clip_store_clear(struct sc_clip_store *store);

#endif
