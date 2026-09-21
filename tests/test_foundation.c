#include "config.h"
#include "clipboard/store.h"
#include "extension.h"
#include "protocol.h"
#include "text.h"

#include <poll.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int failures;

#define CHECK(expr) do { \
	if (!(expr)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expr); \
		failures++; \
	} \
} while (0)

static const char *
fake_extension_path(void)
{
	static char path[1024];
	char cwd[768];
	int n;

	if (getcwd(cwd, sizeof(cwd)) == NULL)
		return NULL;
	n = snprintf(path, sizeof(path), "%s/tests/fake_extension", cwd);
	return n > 0 && (size_t)n < sizeof(path) ? path : NULL;
}

static void
test_protocol(void)
{
	struct sc_record record;
	char *escaped = NULL;
	size_t len = 0;
	char oversized[SUPERCLIP_MAX_RECORD + 1];

	CHECK(sc_protocol_parse("ITEM\t7\tid\ttitle\tdescription\n", 28, &record) == 0);
	if (record.fields != NULL) {
		CHECK(record.kind == SC_RECORD_ITEM);
		CHECK(record.nfields == 5);
		CHECK(strcmp(record.fields[3], "title") == 0);
		sc_record_free(&record);
	}
	CHECK(sc_protocol_escape("a\tb\n\\", 5, &escaped, &len) == 0);
	CHECK(len == 8);
	CHECK(strcmp(escaped, "a\\tb\\n\\\\") == 0);
	free(escaped);
	CHECK(sc_protocol_parse("ITEM\t7\tid\ttitle\n", 16, &record) < 0);
	CHECK(sc_protocol_parse("BOGUS\n", 6, &record) < 0);
	CHECK(sc_protocol_parse("BEGIN\t7\\q\n", 10, &record) < 0);
	memset(oversized, 'a', sizeof(oversized));
	oversized[sizeof(oversized) - 1] = '\n';
	CHECK(sc_protocol_parse(oversized, sizeof(oversized), &record) < 0);
}

static void
test_text(void)
{
	struct sc_text text;
	const char sample[] = "a\314\210\360\237\221\250\342\200\215\360\237\222\273\360\237\207\272\360\237\207\270z";

	CHECK(sc_text_init(&text, SUPERCLIP_MAX_QUERY) == 0);
	CHECK(sc_text_set(&text, sample, sizeof(sample) - 1, SUPERCLIP_MAX_QUERY) == 0);
	sc_text_left(&text);
	CHECK(text.cursor < text.len);
	sc_text_backspace(&text);
	CHECK(strcmp(text.data, "a\314\210\360\237\221\250\342\200\215\360\237\222\273z") == 0);
	sc_text_home(&text);
	sc_text_delete(&text);
	CHECK(strcmp(text.data, "\360\237\221\250\342\200\215\360\237\222\273z") == 0);
	CHECK(sc_text_insert(&text, "\377", 1, SUPERCLIP_MAX_QUERY) < 0);
	sc_text_free(&text);
}

static int
read_one(struct sc_child *child, struct sc_record *record)
{
	struct pollfd pfd;
	int ret;

	pfd.fd = child->out_fd;
	pfd.events = POLLIN | POLLHUP;
	pfd.revents = 0;
	for (;;) {
		ret = sc_child_read_record(child, record);
		if (ret != 0)
			return ret;
		if (poll(&pfd, 1, 2000) <= 0)
			return 0;
	}
}

static void
test_extensions(void)
{
	char tmp[] = "/tmp/superclip-test.XXXXXX";
	char user[256], system[256], userpath[512], syspath[512];
	struct sc_extensions extensions;
	struct sc_extension *fake;
	struct sc_child child;
	struct sc_record record;

	CHECK(mkdtemp(tmp) != NULL);
	(void)snprintf(user, sizeof(user), "%s/user", tmp);
	(void)snprintf(system, sizeof(system), "%s/system", tmp);
	CHECK(mkdir(user, 0700) == 0);
	CHECK(mkdir(system, 0700) == 0);
	(void)snprintf(userpath, sizeof(userpath), "%s/fake", user);
	(void)snprintf(syspath, sizeof(syspath), "%s/other", system);
	CHECK(symlink("/bin/true", userpath) == 0);
	CHECK(symlink("/bin/true", syspath) == 0);
	CHECK(sc_extensions_discover(&extensions, user, system) == 0);
	CHECK(extensions.len == 2);
	CHECK(sc_extensions_find(&extensions, "fake") != NULL);
	CHECK(sc_extension_name_valid("a-b.c_1"));
	CHECK(!sc_extension_name_valid("setup"));
	CHECK(!sc_extension_name_valid("../bad"));
	sc_extensions_free(&extensions);

	memset(&record, 0, sizeof(record));
	CHECK(fake_extension_path() != NULL);
	CHECK(sc_child_spawn(&child, fake_extension_path(),
	    "--superclip-describe") == 0);
	if (child.out_fd >= 0)
		CHECK(read_one(&child, &record) == 1);
	fake = calloc(1, sizeof(*fake));
	CHECK(fake != NULL);
	if (fake != NULL && record.fields != NULL) {
		fake->name = strdup("fake");
		CHECK(fake->name != NULL);
		CHECK(sc_extension_apply_description(fake, &record) == 0);
		CHECK(fake->mode == SC_PROCESS_SHORT);
		CHECK(fake->trigger == SC_TRIGGER_ENTER);
		CHECK(strcmp(fake->title, "Fake extension") == 0);
		free(fake->name);
		free(fake->title);
		free(fake);
	}
	sc_record_free(&record);
	sc_child_close(&child);
	CHECK(unlink(userpath) == 0);
	CHECK(unlink(syspath) == 0);
	CHECK(rmdir(user) == 0);
	CHECK(rmdir(system) == 0);
	CHECK(rmdir(tmp) == 0);
}

static long long
monotonic_millis(void)
{
	struct timespec ts;

	CHECK(clock_gettime(CLOCK_MONOTONIC, &ts) == 0);
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void
test_child_failures(void)
{
	struct sc_child child;
	struct sc_record record;
	const char *fields[] = { "QUERY", "1", "" };
	long long start;

	memset(&record, 0, sizeof(record));
	CHECK(sc_child_spawn(&child, fake_extension_path(), "--malformed") == 0);
	if (child.out_fd >= 0)
		CHECK(read_one(&child, &record) < 0);
	sc_child_close(&child);
	CHECK(sc_child_spawn(&child, fake_extension_path(), "--hang") == 0);
	start = monotonic_millis();
	sc_child_close(&child);
	CHECK(monotonic_millis() - start < 1000);
	CHECK(sc_child_spawn(&child, fake_extension_path(), "--oversized") == 0);
	memset(&record, 0, sizeof(record));
	if (child.out_fd >= 0)
		CHECK(read_one(&child, &record) < 0);
	sc_child_close(&child);
	CHECK(sc_child_spawn(&child, fake_extension_path(), "--early-exit") == 0);
	memset(&record, 0, sizeof(record));
	if (child.out_fd >= 0)
		CHECK(read_one(&child, &record) <= 0);
	sc_child_close(&child);
	CHECK(sc_child_spawn(&child, fake_extension_path(), "--action-error") == 0);
	memset(&record, 0, sizeof(record));
	if (child.out_fd >= 0) {
		CHECK(read_one(&child, &record) == 1);
		CHECK(record.kind == SC_RECORD_ERROR);
		sc_record_free(&record);
	}
	sc_child_close(&child);
	CHECK(sc_child_spawn(&child, fake_extension_path(), "--slow") == 0);
	memset(&record, 0, sizeof(record));
	if (child.out_fd >= 0)
		CHECK(read_one(&child, &record) == 1);
	sc_record_free(&record);
	sc_child_close(&child);
	CHECK(sc_child_spawn(&child, fake_extension_path(), "--persistent") == 0);
	CHECK(sc_child_queue(&child, fields, 3, 0) == 0);
	CHECK(sc_child_flush(&child) >= 0);
	memset(&record, 0, sizeof(record));
	if (child.out_fd >= 0)
		CHECK(read_one(&child, &record) == 1 && record.kind == SC_RECORD_BEGIN);
	sc_record_free(&record);
	sc_child_close(&child);
}

static void
test_child_queue_compaction(void)
{
	struct sc_child child;
	const char *fields[] = { "QUIT" };
	int fds[2];

	CHECK(pipe(fds) == 0);
	memset(&child, 0, sizeof(child));
	child.in_fd = fds[1];
	child.out_fd = child.err_fd = -1;
	child.write_buf = malloc(6);
	CHECK(child.write_buf != NULL);
	if (child.write_buf != NULL) {
		memcpy(child.write_buf, "abcdef", 6);
		child.write_len = 6;
		child.write_off = 3;
		CHECK(sc_child_queue(&child, fields, 1, 0) == 0);
		CHECK(child.write_len == 8);
		CHECK(memcmp(child.write_buf, "defQUIT\n", 8) == 0);
	}
	sc_child_close(&child);
	CHECK(close(fds[0]) == 0);
}

static void
test_store(void)
{
	char tmp[] = "/tmp/superclip-store.XXXXXX";
	char path[512];
	char item[32];
	struct sc_clip_store store;
	const struct sc_clip_entry *entry;
	size_t i;
	int fd;

	CHECK(mkdtemp(tmp) != NULL);
	(void)snprintf(path, sizeof(path), "%s/history", tmp);
	CHECK(sc_clip_store_open(&store, path) == 0);
	CHECK(sc_clip_store_add(&store, "first", 5) == 1);
	CHECK(sc_clip_store_add(&store, "first", 5) == 0);
	CHECK(store.len == 1);
	CHECK(sc_clip_store_add(&store, " \t\343\200\200trimmed\342\200\202\n", 16) == 1);
	CHECK(store.entries[0].len == 7);
	CHECK(strcmp(store.entries[0].text, "trimmed") == 0);
	CHECK(sc_clip_store_add(&store, "\t \343\200\200\n", 6) == 0);
	CHECK(sc_clip_store_add(&store, "second", 6) == 1);
	entry = sc_clip_store_find(&store, 3);
	CHECK(entry != NULL && strcmp(entry->text, "second") == 0);
	sc_clip_store_close(&store);
	CHECK(sc_clip_store_open(&store, path) == 0);
	CHECK(store.len == 3);
	CHECK(strcmp(store.entries[0].text, "second") == 0);
	sc_clip_store_close(&store);
	fd = open(path, O_WRONLY | O_APPEND);
	CHECK(fd >= 0);
	if (fd >= 0) {
		CHECK(write(fd, "x", 1) == 1);
		CHECK(close(fd) == 0);
	}
	CHECK(sc_clip_store_open(&store, path) == 0);
	CHECK(store.len == 3);
	for (i = 0; i < SC_CLIP_MAX_ENTRIES + 1; i++) {
		(void)snprintf(item, sizeof(item), "item-%zu", i);
		CHECK(sc_clip_store_add(&store, item, strlen(item)) >= 0);
	}
	CHECK(store.len == SC_CLIP_MAX_ENTRIES);
	CHECK(sc_clip_store_clear(&store) == 0);
	CHECK(store.len == 0);
	sc_clip_store_close(&store);
	CHECK(rmdir(tmp) == 0);
}

int
main(void)
{
	test_protocol();
	test_text();
	test_extensions();
	test_child_failures();
	test_child_queue_compaction();
	test_store();
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
