#ifndef SUPERCLIP_EXTENSION_H
#define SUPERCLIP_EXTENSION_H

#include "protocol.h"

#include <stddef.h>
#include <sys/types.h>

enum sc_process_mode {
	SC_PROCESS_SHORT,
	SC_PROCESS_PERSISTENT
};

enum sc_trigger {
	SC_TRIGGER_ENTER,
	SC_TRIGGER_CHANGE
};

struct sc_extension {
	char *name;
	char *path;
	char *title;
	enum sc_process_mode mode;
	enum sc_trigger trigger;
};

struct sc_extensions {
	struct sc_extension *items;
	size_t len;
	size_t cap;
};

struct sc_child {
	pid_t pid;
	int in_fd;
	int out_fd;
	int err_fd;
	int input_closed;
	char *read_buf;
	size_t read_len;
	size_t read_off;
	char *write_buf;
	size_t write_len;
	size_t write_off;
	char *stderr_buf;
	size_t stderr_len;
	int exited;
	int status;
};

int sc_extension_name_valid(const char *name);
int sc_extensions_discover(struct sc_extensions *out, const char *user_dir, const char *system_dir);
void sc_extensions_free(struct sc_extensions *extensions);
struct sc_extension *sc_extensions_find(struct sc_extensions *extensions, const char *name);
int sc_extension_apply_description(struct sc_extension *extension, const struct sc_record *record);

int sc_child_spawn(struct sc_child *child, const char *path, const char *command);
int sc_child_queue(struct sc_child *child, const char *const *fields, size_t nfields, int close_after);
int sc_child_flush(struct sc_child *child);
int sc_child_read_record(struct sc_child *child, struct sc_record *record);
int sc_child_drain_stderr(struct sc_child *child);
int sc_child_reap(struct sc_child *child, int block);
void sc_child_close(struct sc_child *child);
int sc_child_quit(struct sc_child *child);

#endif
