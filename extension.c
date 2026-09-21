#include "config.h"
#include "extension.h"
#include "protocol.h"
#include "util.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pwd.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

static void
extension_free(struct sc_extension *extension)
{
	if (extension == NULL)
		return;
	free(extension->name);
	free(extension->path);
	free(extension->title);
	memset(extension, 0, sizeof(*extension));
}

int
sc_extension_name_valid(const char *name)
{
	size_t i;

	if (name == NULL || name[0] == '\0' || strcmp(name, "setup") == 0)
		return 0;
	if (!((name[0] >= 'A' && name[0] <= 'Z') ||
	    (name[0] >= 'a' && name[0] <= 'z') ||
	    (name[0] >= '0' && name[0] <= '9')))
		return 0;
	for (i = 1; name[i] != '\0'; i++) {
		if (!((name[i] >= 'A' && name[i] <= 'Z') ||
		    (name[i] >= 'a' && name[i] <= 'z') ||
		    (name[i] >= '0' && name[i] <= '9') ||
		    name[i] == '.' || name[i] == '_' || name[i] == '-'))
			return 0;
	}
	return 1;
}

static int
extensions_reserve(struct sc_extensions *extensions, size_t want)
{
	struct sc_extension *next;
	size_t cap;

	if (want <= extensions->cap)
		return 0;
	cap = extensions->cap == 0 ? 8 : extensions->cap * 2;
	if (cap < want || cap > SIZE_MAX / sizeof(*next))
		return -1;
	next = realloc(extensions->items, cap * sizeof(*next));
	if (next == NULL)
		return -1;
	extensions->items = next;
	extensions->cap = cap;
	return 0;
}

struct sc_extension *
sc_extensions_find(struct sc_extensions *extensions, const char *name)
{
	size_t i;

	if (extensions == NULL || name == NULL)
		return NULL;
	for (i = 0; i < extensions->len; i++)
		if (strcmp(extensions->items[i].name, name) == 0)
			return &extensions->items[i];
	return NULL;
}

static int
discover_dir(struct sc_extensions *extensions, const char *dir)
{
	DIR *stream;
	struct dirent *entry;
	struct stat st;
	struct sc_extension extension;
	char joined[PATH_MAX];
	char resolved[PATH_MAX];
	int ret = -1;

	if (dir == NULL)
		return 0;
	stream = opendir(dir);
	if (stream == NULL)
		return errno == ENOENT ? 0 : -1;
	while ((entry = readdir(stream)) != NULL) {
		if (!sc_extension_name_valid(entry->d_name) ||
		    sc_extensions_find(extensions, entry->d_name) != NULL)
			continue;
		if (snprintf(joined, sizeof(joined), "%s/%s", dir, entry->d_name) < 0 ||
		    strlen(joined) >= sizeof(joined))
			continue;
		if (realpath(joined, resolved) == NULL || stat(resolved, &st) < 0 ||
		    !S_ISREG(st.st_mode) || access(resolved, X_OK) < 0)
			continue;
		memset(&extension, 0, sizeof(extension));
		extension.name = sc_xstrndup(entry->d_name, strlen(entry->d_name));
		extension.path = sc_xstrndup(resolved, strlen(resolved));
		if (extension.name == NULL || extension.path == NULL || extensions_reserve(extensions, extensions->len + 1) < 0) {
			extension_free(&extension);
			goto done;
		}
		extensions->items[extensions->len++] = extension;
	}
	ret = 0;
done:
	closedir(stream);
	return ret;
}

int
sc_extensions_discover(struct sc_extensions *out, const char *user_dir, const char *system_dir)
{
	if (out == NULL)
		return -1;
	memset(out, 0, sizeof(*out));
	if (discover_dir(out, user_dir) < 0 || discover_dir(out, system_dir) < 0) {
		sc_extensions_free(out);
		return -1;
	}
	return 0;
}

void
sc_extensions_free(struct sc_extensions *extensions)
{
	size_t i;

	if (extensions == NULL)
		return;
	for (i = 0; i < extensions->len; i++)
		extension_free(&extensions->items[i]);
	free(extensions->items);
	memset(extensions, 0, sizeof(*extensions));
}

int
sc_extension_apply_description(struct sc_extension *extension, const struct sc_record *record)
{
	char *title;

	if (extension == NULL || record == NULL || record->kind != SC_RECORD_SUPERCLIP ||
	    record->nfields != 6 || strcmp(record->fields[1], "1") != 0 ||
	    strcmp(record->fields[2], extension->name) != 0)
		return -1;
	if (strcmp(record->fields[3], "short") == 0)
		extension->mode = SC_PROCESS_SHORT;
	else if (strcmp(record->fields[3], "persistent") == 0)
		extension->mode = SC_PROCESS_PERSISTENT;
	else
		return -1;
	if (strcmp(record->fields[4], "enter") == 0)
		extension->trigger = SC_TRIGGER_ENTER;
	else if (strcmp(record->fields[4], "change") == 0)
		extension->trigger = SC_TRIGGER_CHANGE;
	else
		return -1;
	if (extension->mode == SC_PROCESS_SHORT && extension->trigger == SC_TRIGGER_CHANGE)
		return -1;
	title = sc_xstrndup(record->fields[5], strlen(record->fields[5]));
	if (title == NULL)
		return -1;
	free(extension->title);
	extension->title = title;
	return 0;
}

static int
pipe_cloexec(int fds[2])
{
	if (pipe(fds) < 0)
		return -1;
	if (fcntl(fds[0], F_SETFD, FD_CLOEXEC) < 0 || fcntl(fds[1], F_SETFD, FD_CLOEXEC) < 0) {
		close(fds[0]);
		close(fds[1]);
		return -1;
	}
	return 0;
}

int
sc_child_spawn(struct sc_child *child, const char *path, const char *command)
{
	int in[2] = { -1, -1 }, out[2] = { -1, -1 }, err[2] = { -1, -1 };
	pid_t pid;
	char *const argv[] = { (char *)path, (char *)command, NULL };

	if (child == NULL || path == NULL || command == NULL || path[0] != '/')
		return -1;
	memset(child, 0, sizeof(*child));
	child->in_fd = child->out_fd = child->err_fd = -1;
	if (pipe_cloexec(in) < 0 || pipe_cloexec(out) < 0 || pipe_cloexec(err) < 0)
		goto fail;
	pid = fork();
	if (pid < 0)
		goto fail;
	if (pid == 0) {
		if (dup2(in[0], STDIN_FILENO) < 0 || dup2(out[1], STDOUT_FILENO) < 0 ||
		    dup2(err[1], STDERR_FILENO) < 0)
			_exit(127);
		close(in[0]); close(in[1]); close(out[0]); close(out[1]); close(err[0]); close(err[1]);
		execve(path, argv, environ);
		_exit(127);
	}
	close(in[0]); close(out[1]); close(err[1]);
	child->pid = pid;
	child->in_fd = in[1];
	child->out_fd = out[0];
	child->err_fd = err[0];
	if (sc_set_nonblock(child->in_fd) < 0 || sc_set_nonblock(child->out_fd) < 0 ||
	    sc_set_nonblock(child->err_fd) < 0) {
		sc_child_close(child);
		return -1;
	}
	return 0;
fail:
	if (in[0] >= 0) close(in[0]);
	if (in[1] >= 0) close(in[1]);
	if (out[0] >= 0) close(out[0]);
	if (out[1] >= 0) close(out[1]);
	if (err[0] >= 0) close(err[0]);
	if (err[1] >= 0) close(err[1]);
	return -1;
}

static int
append_bytes(char **buf, size_t *len, size_t *off, const char *data, size_t n, size_t limit)
{
	char *next;

	if (n > limit - (*len - *off))
		return -1;
	if (*off != 0) {
		memmove(*buf, *buf + *off, *len - *off);
		*len -= *off;
		*off = 0;
	}
	next = realloc(*buf, *len + n + 1);
	if (next == NULL)
		return -1;
	*buf = next;
	memcpy(*buf + *len, data, n);
	*len += n;
	(*buf)[*len] = '\0';
	return 0;
}

int
sc_child_queue(struct sc_child *child, const char *const *fields, size_t nfields, int close_after)
{
	char *record = NULL, *escaped = NULL;
	size_t i, used = 0, escaped_len;
	char *next;

	if (child == NULL || fields == NULL || nfields == 0 || child->in_fd < 0 || child->input_closed)
		return -1;
	for (i = 0; i < nfields; i++) {
		if (fields[i] == NULL || !sc_utf8_valid(fields[i], strlen(fields[i])) ||
		    strlen(fields[i]) > SUPERCLIP_MAX_RECORD)
			return -1;
	}
	record = malloc(SUPERCLIP_MAX_RECORD);
	if (record == NULL)
		return -1;
	for (i = 0; i < nfields; i++) {
		if (sc_protocol_escape(fields[i], strlen(fields[i]), &escaped, &escaped_len) < 0 ||
		    escaped_len > SUPERCLIP_MAX_RECORD - used - 1)
			goto fail;
		memcpy(record + used, escaped, escaped_len);
		used += escaped_len;
		free(escaped);
		escaped = NULL;
		record[used++] = i + 1 == nfields ? '\n' : '\t';
	}
	if (used > SUPERCLIP_MAX_RECORD * 2U - (child->write_len - child->write_off))
		goto fail;
	if (child->write_off != 0)
		memmove(child->write_buf, child->write_buf + child->write_off,
		    child->write_len - child->write_off);
	child->write_len -= child->write_off;
	child->write_off = 0;
	next = realloc(child->write_buf, child->write_len + used);
	if (next == NULL)
		goto fail;
	child->write_buf = next;
	memcpy(child->write_buf + child->write_len, record, used);
	child->write_len += used;
	if (close_after)
		child->input_closed = 2;
	free(record);
	return 0;
fail:
	free(escaped);
	free(record);
	return -1;
}

int
sc_child_flush(struct sc_child *child)
{
	ssize_t n;

	if (child == NULL || child->in_fd < 0)
		return -1;
	while (child->write_off < child->write_len) {
		n = write(child->in_fd, child->write_buf + child->write_off, child->write_len - child->write_off);
		if (n > 0) {
			child->write_off += (size_t)n;
			continue;
		}
		if (n < 0 && errno == EINTR)
			continue;
		if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
			return 0;
		return -1;
	}
	free(child->write_buf);
	child->write_buf = NULL;
	child->write_len = child->write_off = 0;
	if (child->input_closed == 2) {
		close(child->in_fd);
		child->in_fd = -1;
		child->input_closed = 1;
	}
	return 1;
}

int
sc_child_read_record(struct sc_child *child, struct sc_record *record)
{
	char chunk[4096];
	char *line;
	ssize_t n;
	size_t i;

	if (child == NULL || record == NULL || child->out_fd < 0)
		return -1;
	for (;;) {
		for (i = child->read_off; i < child->read_len; i++) {
			if (child->read_buf[i] == '\n') {
				line = child->read_buf + child->read_off;
				if (sc_protocol_parse(line, i - child->read_off + 1, record) < 0)
					return -1;
				child->read_off = i + 1;
				if (child->read_off == child->read_len) {
					child->read_off = child->read_len = 0;
				}
				return 1;
			}
		}
		if (child->read_len - child->read_off >= SUPERCLIP_MAX_RECORD)
			return -1;
		n = read(child->out_fd, chunk, sizeof(chunk));
		if (n > 0) {
			if (append_bytes(&child->read_buf, &child->read_len, &child->read_off, chunk, (size_t)n, SUPERCLIP_MAX_RECORD) < 0)
				return -1;
			continue;
		}
		if (n == 0) {
			close(child->out_fd);
			child->out_fd = -1;
			return child->read_len == child->read_off ? 0 : -1;
		}
		if (errno == EINTR)
			continue;
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return 0;
		return -1;
	}
}

int
sc_child_drain_stderr(struct sc_child *child)
{
	char buf[1024];
	ssize_t n;
	size_t keep;

	if (child == NULL || child->err_fd < 0)
		return -1;
	for (;;) {
		n = read(child->err_fd, buf, sizeof(buf));
		if (n > 0) {
			if (child->stderr_len < SUPERCLIP_MAX_STDERR) {
				keep = (size_t)n;
				if (keep > SUPERCLIP_MAX_STDERR - child->stderr_len)
					keep = SUPERCLIP_MAX_STDERR - child->stderr_len;
				if (append_bytes(&child->stderr_buf, &child->stderr_len, &(size_t){0}, buf, keep, SUPERCLIP_MAX_STDERR) < 0)
					return -1;
			}
			continue;
		}
		if (n == 0) {
			close(child->err_fd);
			child->err_fd = -1;
			return 0;
		}
		if (errno == EINTR)
			continue;
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return 0;
		return -1;
	}
}

int
sc_child_reap(struct sc_child *child, int block)
{
	pid_t ret;
	int options = block ? 0 : WNOHANG;

	if (child == NULL || child->pid <= 0 || child->exited)
		return 0;
	do {
		ret = waitpid(child->pid, &child->status, options);
	} while (ret < 0 && errno == EINTR);
	if (ret == child->pid) {
		child->exited = 1;
		return 1;
	}
	return ret == 0 ? 0 : -1;
}

void
sc_child_close(struct sc_child *child)
{
	struct timespec pause = { 0, 10000000L };
	int i;

	if (child == NULL)
		return;
	if (child->in_fd >= 0) close(child->in_fd);
	if (child->out_fd >= 0) close(child->out_fd);
	if (child->err_fd >= 0) close(child->err_fd);
	if (child->pid > 0 && !child->exited) {
		(void)kill(child->pid, SIGTERM);
		for (i = 0; i < SUPERCLIP_CHILD_GRACE_MS / 10; i++) {
			if (sc_child_reap(child, 0) != 0)
				break;
			(void)nanosleep(&pause, NULL);
		}
		if (!child->exited)
			(void)kill(child->pid, SIGKILL);
		(void)sc_child_reap(child, 1);
	}
	free(child->read_buf);
	free(child->write_buf);
	free(child->stderr_buf);
	memset(child, 0, sizeof(*child));
	child->in_fd = child->out_fd = child->err_fd = -1;
}
