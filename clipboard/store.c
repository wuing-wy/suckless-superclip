#include "store.h"
#include "util.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define SC_CLIP_MAGIC 0x53434c50U

struct disk_header {
	uint32_t magic;
	uint32_t len;
	uint64_t id;
};

static void
entry_free(struct sc_clip_entry *entry)
{
	free(entry->text);
	memset(entry, 0, sizeof(*entry));
}

/* Returns 1 for a complete read, 0 for clean EOF before any byte, and -1 otherwise. */
static int
read_exact(int fd, void *buf, size_t len)
{
	unsigned char *p = buf;
	ssize_t n;
	size_t used = 0;

	while (used < len) {
		n = read(fd, p + used, len - used);
		if (n > 0) {
			used += (size_t)n;
			continue;
		}
		if (n == 0)
			return used == 0 ? 0 : -1;
		if (errno == EINTR)
			continue;
		return -1;
	}
	return 1;
}

static int
append_entry(const struct sc_clip_store *store, const struct sc_clip_entry *entry)
{
	struct disk_header header;
	int fd;

	fd = open(store->path, O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (fd < 0)
		return -1;
	if (fchmod(fd, 0600) < 0) {
		close(fd);
		return -1;
	}
	header.magic = SC_CLIP_MAGIC;
	header.len = (uint32_t)entry->len;
	header.id = entry->id;
	if (sc_write_all(fd, &header, sizeof(header)) < 0 ||
	    sc_write_all(fd, entry->text, entry->len) < 0 || fsync(fd) < 0) {
		close(fd);
		return -1;
	}
	close(fd);
	return 0;
}

static int
compact_store(const struct sc_clip_store *store)
{
	char *tmp;
	struct disk_header header;
	int fd;
	size_t i;

	tmp = malloc(strlen(store->path) + sizeof(".tmp.XXXXXX"));
	if (tmp == NULL)
		return -1;
	(void)snprintf(tmp, strlen(store->path) + sizeof(".tmp.XXXXXX"), "%s.tmp.XXXXXX", store->path);
	fd = mkstemp(tmp);
	if (fd < 0)
		goto fail;
	if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0 || fchmod(fd, 0600) < 0)
		goto fail_close;
	for (i = store->len; i > 0; i--) {
		const struct sc_clip_entry *entry = &store->entries[i - 1];
		header.magic = SC_CLIP_MAGIC;
		header.len = (uint32_t)entry->len;
		header.id = entry->id;
		if (sc_write_all(fd, &header, sizeof(header)) < 0 ||
		    sc_write_all(fd, entry->text, entry->len) < 0)
			goto fail_close;
	}
	if (fsync(fd) < 0 || close(fd) < 0)
		goto fail;
	if (rename(tmp, store->path) < 0)
		goto fail;
	free(tmp);
	return 0;
fail_close:
	close(fd);
fail:
	unlink(tmp);
	free(tmp);
	return -1;
}

int
sc_clip_store_open(struct sc_clip_store *store, const char *path)
{
	struct disk_header header;
	struct stat st;
	int fd;
	int ret;
	char *text;

	if (store == NULL || path == NULL)
		return -1;
	memset(store, 0, sizeof(*store));
	store->next_id = 1;
	store->path = sc_xstrndup(path, strlen(path));
	if (store->path == NULL)
		return -1;
	fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0)
		return errno == ENOENT ? 0 : -1;
	if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode) || st.st_uid != getuid() ||
	    (st.st_mode & 0077) != 0 || st.st_size > SC_CLIP_MAX_STATE)
		goto fail;
	for (;;) {
		ret = read_exact(fd, &header, sizeof(header));
		if (ret == 0)
			break;
		if (ret < 0 || header.magic != SC_CLIP_MAGIC ||
		    header.len == 0 || header.len > SC_CLIP_MAX_ENTRY)
			break;
		text = malloc((size_t)header.len + 1);
		if (text == NULL)
			goto fail;
		if (read_exact(fd, text, header.len) != 1 || !sc_utf8_valid(text, header.len)) {
			free(text);
			break;
		}
		text[header.len] = '\0';
		if (store->len == SC_CLIP_MAX_ENTRIES)
			entry_free(&store->entries[store->len - 1]);
		else
			store->len++;
		memmove(&store->entries[1], &store->entries[0],
		    (store->len - 1) * sizeof(store->entries[0]));
		store->entries[0].id = header.id;
		store->entries[0].text = text;
		store->entries[0].len = header.len;
		if (header.id >= store->next_id)
			store->next_id = header.id + 1;
	}
	close(fd);
	if (store->next_id == 0)
		store->next_id = 1;
	return 0;
fail:
	close(fd);
	sc_clip_store_close(store);
	return -1;
}

void
sc_clip_store_close(struct sc_clip_store *store)
{
	size_t i;

	if (store == NULL)
		return;
	for (i = 0; i < store->len; i++)
		entry_free(&store->entries[i]);
	free(store->path);
	memset(store, 0, sizeof(*store));
}

int
sc_clip_store_add(struct sc_clip_store *store, const char *text, size_t len)
{
	struct sc_clip_entry entry;
	struct stat st;
	size_t start, trimmed_len;

	if (store == NULL || text == NULL || len == 0 || len > SC_CLIP_MAX_ENTRY ||
	    sc_utf8_trim_whitespace(text, len, &start, &trimmed_len) < 0 || trimmed_len == 0)
		return 0;
	text += start;
	len = trimmed_len;
	if (store->len != 0 && store->entries[0].len == len &&
	    memcmp(store->entries[0].text, text, len) == 0)
		return 0;
	memset(&entry, 0, sizeof(entry));
	entry.text = sc_xstrndup(text, len);
	if (entry.text == NULL)
		return -1;
	entry.len = len;
	entry.id = store->next_id++;
	if (append_entry(store, &entry) < 0) {
		entry_free(&entry);
		return -1;
	}
	if (store->len == SC_CLIP_MAX_ENTRIES)
		entry_free(&store->entries[store->len - 1]);
	else
		store->len++;
	memmove(&store->entries[1], &store->entries[0],
	    (store->len - 1) * sizeof(store->entries[0]));
	store->entries[0] = entry;
	if (stat(store->path, &st) == 0 && st.st_size > SC_CLIP_MAX_STATE)
		(void)compact_store(store);
	return 1;
}

const struct sc_clip_entry *
sc_clip_store_find(const struct sc_clip_store *store, uint64_t id)
{
	size_t i;

	if (store == NULL)
		return NULL;
	for (i = 0; i < store->len; i++)
		if (store->entries[i].id == id)
			return &store->entries[i];
	return NULL;
}

int
sc_clip_store_clear(struct sc_clip_store *store)
{
	size_t i;

	if (store == NULL)
		return -1;
	for (i = 0; i < store->len; i++)
		entry_free(&store->entries[i]);
	store->len = 0;
	if (unlink(store->path) < 0 && errno != ENOENT)
		return -1;
	return 0;
}
