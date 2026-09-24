#include "protocol.h"
#include "util.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define SNIPPETS_TITLE_MAX 512U
#define SNIPPETS_BODY_MAX 65536U
#define SNIPPETS_RAW_MAX 131072U
#define SNIPPETS_FILE_MAX (1024U * 1024U)
#define SNIPPETS_SCAN_MAX 1024U
#define SNIPPETS_KEEP_MAX 256U
#define SNIPPETS_PREVIEW_MAX 120U
#define SNIPPETS_HOLDER_GRACE_MS 200

struct snippet {
	unsigned long lineno;
	char *title;
	char *body;
};

struct snippet_set {
	struct snippet items[SNIPPETS_KEEP_MAX];
	size_t len;
	unsigned long skipped;
};

static char warned_path[PATH_MAX];
static int warned_set;

static void
free_set(struct snippet_set *set)
{
	size_t i;

	for (i = 0; i < set->len; i++) {
		free(set->items[i].title);
		free(set->items[i].body);
	}
	memset(set, 0, sizeof(*set));
}

/* First raw TAB splits title/body. Backslash only escapes protocol sequences;
 * a literal "\t" (backslash + t) is still split on the raw TAB. Scan titles
 * for raw TAB directly; only skip the escape's second byte when validating. */
static const char *
find_split(const char *line, size_t len)
{
	size_t i;

	for (i = 0; i < len; i++) {
		if (line[i] == '\t')
			return line + i;
	}
	return NULL;
}

static int
unescape_field(const char *in, size_t len, char **out)
{
	char *buf;
	size_t i, n = 0;

	buf = malloc(len + 1);
	if (buf == NULL)
		return -1;
	for (i = 0; i < len; i++) {
		if (in[i] != '\\') {
			buf[n++] = in[i];
			continue;
		}
		if (++i >= len) {
			free(buf);
			return -1;
		}
		switch (in[i]) {
		case '\\': buf[n++] = '\\'; break;
		case 't': buf[n++] = '\t'; break;
		case 'n': buf[n++] = '\n'; break;
		case 'r': buf[n++] = '\r'; break;
		default:
			free(buf);
			return -1;
		}
	}
	buf[n] = '\0';
	if (!sc_utf8_valid(buf, n) && n != 0) {
		free(buf);
		return -1;
	}
	*out = buf;
	return 0;
}

static int
title_ok(const char *title)
{
	size_t len;

	if (title == NULL || title[0] == '\0')
		return 0;
	len = strlen(title);
	if (len > SNIPPETS_TITLE_MAX)
		return 0;
	return strchr(title, '\t') == NULL && strchr(title, '\n') == NULL &&
	    strchr(title, '\r') == NULL;
}

static int
blank_or_comment(const char *line, size_t len)
{
	size_t i;

	for (i = 0; i < len; i++) {
		if (line[i] == ' ' || line[i] == '\t' || line[i] == '\r')
			continue;
		return line[i] == '\n' || line[i] == '#';
	}
	return 1;
}

static int
snippets_path(char *out, size_t cap)
{
	const char *xdg = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");
	int n;

	if (xdg != NULL && xdg[0] != '\0')
		n = snprintf(out, cap, "%s/superclip/snippets", xdg);
	else if (home != NULL && home[0] != '\0')
		n = snprintf(out, cap, "%s/.config/superclip/snippets", home);
	else
		return -1;
	return n > 0 && (size_t)n < cap ? 0 : -1;
}

static int
warn_readable(const char *path)
{
	struct stat st;

	if (warned_set && strcmp(warned_path, path) == 0)
		return 0;
	if (stat(path, &st) == 0 && (st.st_mode & 0077) != 0)
		fprintf(stderr, "snippets: %s is group/world accessible\n", path);
	if (strlen(path) < sizeof(warned_path)) {
		memcpy(warned_path, path, strlen(path) + 1);
		warned_set = 1;
	}
	return 0;
}

/*
 * Re-read on every request so edits apply immediately; no cache.
 * Missing file (ENOENT) is a valid empty set. Other errors are reported
 * per request while the session survives.
 */
static int
load_snippets(struct snippet_set *set, const char **fail)
{
	char path[PATH_MAX];
	struct stat st;
	FILE *fp;
	char *raw = NULL;
	size_t cap = 0, len;
	unsigned long lineno = 0;

	memset(set, 0, sizeof(*set));
	*fail = NULL;
	if (snippets_path(path, sizeof(path)) < 0) {
		*fail = "cannot read snippets";
		return -1;
	}
	if (stat(path, &st) < 0) {
		if (errno == ENOENT)
			return 0;
		*fail = "cannot read snippets";
		return -1;
	}
	if (!S_ISREG(st.st_mode) || (size_t)st.st_size > SNIPPETS_FILE_MAX) {
		*fail = (size_t)st.st_size > SNIPPETS_FILE_MAX ?
		    "snippets file too large" : "cannot read snippets";
		return -1;
	}
	fp = fopen(path, "r");
	if (fp == NULL) {
		*fail = "cannot read snippets";
		return -1;
	}
	(void)warn_readable(path);
	while (lineno < SNIPPETS_SCAN_MAX) {
		const char *split, *raw_title, *raw_body;
		size_t raw_title_len, raw_body_len;
		char *title = NULL, *body = NULL;
		ssize_t n;

		n = getline(&raw, &cap, fp);
		if (n < 0)
			break;
		if ((size_t)n > SNIPPETS_RAW_MAX + 1) {
			set->skipped++;
			while (n > 0 && raw[(size_t)n - 1] != '\n') {
				n = getline(&raw, &cap, fp);
				if (n < 0)
					break;
			}
			lineno++;
			continue;
		}
		len = (size_t)n;
		if (len != 0 && raw[len - 1] == '\n')
			len--;
		lineno++;
		if (blank_or_comment(raw, len))
			continue;
		split = find_split(raw, len);
		if (split == NULL) {
			set->skipped++;
			continue;
		}
		raw_title = raw;
		raw_title_len = (size_t)(split - raw);
		raw_body = split + 1;
		raw_body_len = len - raw_title_len - 1;
		if (unescape_field(raw_title, raw_title_len, &title) < 0 ||
		    unescape_field(raw_body, raw_body_len, &body) < 0 ||
		    !title_ok(title) || strlen(body) > SNIPPETS_BODY_MAX) {
			free(title);
			free(body);
			set->skipped++;
			continue;
		}
		if (set->len == SNIPPETS_KEEP_MAX) {
			free(title);
			free(body);
			continue;
		}
		set->items[set->len].lineno = lineno;
		set->items[set->len].title = title;
		set->items[set->len].body = body;
		set->len++;
	}
	free(raw);
	fclose(fp);
	return 0;
}

static void
preview_body(const char *body, char *out, size_t cap)
{
	size_t i = 0, n = 0;

	while (body[i] != '\0' && body[i] != '\n' && n + 1 < cap && n < SNIPPETS_PREVIEW_MAX) {
		size_t left = SNIPPETS_PREVIEW_MAX - n;
		unsigned char c = (unsigned char)body[i];
		size_t width = 1;

		if (c >= 0x80U) {
			if ((c & 0xe0U) == 0xc0U)
				width = 2;
			else if ((c & 0xf0U) == 0xe0U)
				width = 3;
			else if ((c & 0xf8U) == 0xf0U)
				width = 4;
			else
				break;
			if (width > left)
				break;
			if (!sc_utf8_valid(body + i, width))
				break;
		}
		if (n + width >= cap)
			break;
		memcpy(out + n, body + i, width);
		n += width;
		i += width;
	}
	out[n] = '\0';
}

static int
write_fields(int fd, const char *const *fields, size_t nfields)
{
	char *wire;
	size_t len;
	int ret;

	if (sc_protocol_serialize(fields, nfields, &wire, &len) < 0)
		return -1;
	ret = sc_write_all(fd, wire, len);
	free(wire);
	return ret;
}

static int
respond_error(int fd, const char *id, const char *message)
{
	const char *fields[] = { "ERROR", id, message };

	return write_fields(fd, fields, 3);
}

static int
respond_query(int fd, const char *id, const char *query)
{
	struct snippet_set set;
	const char *fail = NULL;
	const char *begin[] = { "BEGIN", id };
	const char *end[] = { "END", id };
	size_t i, shown = 0;

	if (load_snippets(&set, &fail) < 0)
		return respond_error(fd, id, fail);
	if (write_fields(fd, begin, 2) < 0) {
		free_set(&set);
		return -1;
	}
	for (i = 0; i < set.len && shown < SNIPPETS_KEEP_MAX; i++) {
		const char *fields[5];
		char rid[640];
		char preview[SNIPPETS_PREVIEW_MAX + 1];
		int n;

		if (query[0] != '\0' && strstr(set.items[i].title, query) == NULL &&
		    strstr(set.items[i].body, query) == NULL)
			continue;
		n = snprintf(rid, sizeof(rid), "%lu:%s", set.items[i].lineno,
		    set.items[i].title);
		if (n < 0 || (size_t)n >= sizeof(rid))
			continue;
		preview_body(set.items[i].body, preview, sizeof(preview));
		fields[0] = "ITEM";
		fields[1] = id;
		fields[2] = rid;
		fields[3] = set.items[i].title;
		fields[4] = preview;
		if (write_fields(fd, fields, 5) < 0) {
			free_set(&set);
			return -1;
		}
		shown++;
	}
	if (set.skipped != 0)
		fprintf(stderr, "snippets: skipped %lu bad lines\n", set.skipped);
	free_set(&set);
	return write_fields(fd, end, 2);
}

static int
holder_pid_path(char *out, size_t cap)
{
	const char *runtime = getenv("XDG_RUNTIME_DIR");
	int n;

	if (runtime != NULL && runtime[0] != '\0')
		n = snprintf(out, cap, "%s/superclip/snippets-holder.pid", runtime);
	else
		n = snprintf(out, cap, "/tmp/superclip-%lu/snippets-holder.pid",
		    (unsigned long)getuid());
	if (n < 0 || (size_t)n >= cap)
		return -1;
	return 0;
}

static int
mkdir_private(const char *path)
{
	struct stat st;

	if (mkdir(path, 0700) < 0 && errno != EEXIST)
		return -1;
	if (stat(path, &st) < 0 || !S_ISDIR(st.st_mode) || st.st_uid != getuid() ||
	    (st.st_mode & 0077) != 0)
		return -1;
	return 0;
}

static int
stop_pid(pid_t pid)
{
	struct timespec pause = { 0, 10000000L };
	int i;

	if (kill(pid, SIGTERM) < 0 && errno != ESRCH)
		return -1;
	for (i = 0; i < SNIPPETS_HOLDER_GRACE_MS / 10; i++) {
		if (kill(pid, 0) < 0)
			return 0;
		(void)nanosleep(&pause, NULL);
	}
	if (kill(pid, 0) == 0 && kill(pid, SIGKILL) < 0 && errno != ESRCH)
		return -1;
	for (i = 0; i < SNIPPETS_HOLDER_GRACE_MS / 10; i++) {
		if (kill(pid, 0) < 0)
			return 0;
		(void)nanosleep(&pause, NULL);
	}
	return kill(pid, 0) < 0 ? 0 : -1;
}

/* Read /proc/<pid>/stat starttime (field 22) for PID-reuse detection. */
static int
proc_starttime(pid_t pid, unsigned long long *starttime)
{
	char path[64];
	char buf[1024];
	char *paren, *rest;
	int fd, field = 0;
	ssize_t n;
	unsigned long long value = 0;

	if (snprintf(path, sizeof(path), "/proc/%ld/stat", (long)pid) < 0 ||
	    strlen(path) >= sizeof(path))
		return -1;
	fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0)
		return -1;
	n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n <= 0)
		return -1;
	buf[n] = '\0';
	paren = strrchr(buf, ')');
	if (paren == NULL)
		return -1;
	rest = paren + 1;
	while (*rest != '\0') {
		while (*rest == ' ')
			rest++;
		if (*rest == '\0')
			break;
		field++;
		if (field == 20) {
			char *end;

			errno = 0;
			value = strtoull(rest, &end, 10);
			if (errno != 0 || end == rest)
				return -1;
			*starttime = value;
			return 0;
		}
		while (*rest != '\0' && *rest != ' ')
			rest++;
	}
	return -1;
}

static int
read_whole(const char *path, char *buf, size_t cap)
{
	int fd;
	ssize_t n;

	if (cap == 0)
		return -1;
	fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0)
		return -1;
	n = read(fd, buf, cap - 1);
	close(fd);
	if (n <= 0)
		return -1;
	buf[n] = '\0';
	return 0;
}

static int
read_pid_entry(const char *path, pid_t *pid, unsigned long long *starttime)
{
	char buf[96];
	char *end, *second;
	long parsed;

	if (read_whole(path, buf, sizeof(buf)) < 0)
		return -1;
	errno = 0;
	parsed = strtol(buf, &end, 10);
	if (errno != 0 || end == buf || parsed <= 0 || parsed > INT_MAX)
		return -1;
	while (*end == ' ' || *end == '\t')
		end++;
	if (*end == '\0' || *end == '\n')
		return -1;
	errno = 0;
	*starttime = strtoull(end, &second, 10);
	if (errno != 0 || second == end)
		return -1;
	*pid = (pid_t)parsed;
	return 0;
}

/*
 * Replace the live holder: kill the recorded pid only when its starttime
 * still matches (PID reuse guard). Parse failure alone never kills; the
 * stale file is removed and the new holder proceeds.
 */
static int
stop_recorded(const char *path)
{
	pid_t pid;
	unsigned long long recorded;

	if (read_pid_entry(path, &pid, &recorded) < 0) {
		(void)unlink(path);
		return 0;
	}
	if (kill(pid, 0) < 0) {
		(void)unlink(path);
		return 0;
	}
	{
		unsigned long long now;

		if (proc_starttime(pid, &now) < 0 || now != recorded) {
			(void)unlink(path);
			return 0;
		}
	}
	if (stop_pid(pid) < 0)
		return -1;
	(void)unlink(path);
	return 0;
}

/* Returns 0 when no live holder remains, -1 on unrecoverable error. */
static int
replace_holder(void)
{
	char path[sizeof(((struct sockaddr_un *)0)->sun_path)];
	char dir[sizeof(path)];
	char *slash;

	if (holder_pid_path(path, sizeof(path)) < 0)
		return -1;
	memcpy(dir, path, sizeof(dir));
	slash = strrchr(dir, '/');
	if (slash == NULL)
		return -1;
	*slash = '\0';
	if (mkdir_private(dir) < 0)
		return -1;
	return stop_recorded(path);
}

static volatile sig_atomic_t holder_stop_flag;

static void
holder_handler(int signal_number)
{
	(void)signal_number;
	holder_stop_flag = 1;
}

static int
write_pid_file(const char *path)
{
	char buf[96];
	unsigned long long starttime;
	int fd, n;

	if (proc_starttime(getpid(), &starttime) < 0)
		return -1;
	n = snprintf(buf, sizeof(buf), "%ld %llu\n", (long)getpid(), starttime);
	if (n < 0 || (size_t)n >= sizeof(buf))
		return -1;
	fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (fd < 0)
		return -1;
	if (sc_write_all(fd, buf, (size_t)n) < 0 || fchmod(fd, 0600) < 0) {
		close(fd);
		(void)unlink(path);
		return -1;
	}
	close(fd);
	return 0;
}

/* Single-threaded owner: TARGETS/TIMESTAMP/UTF8/STRING once, rest refused. */
static int
holder_main(const char *text, size_t len, int ready_fd, const char *pid_path)
{
	Display *display;
	Window window;
	Atom clipboard, utf8, targets, timestamp;
	XEvent event;
	struct sigaction action;
	struct pollfd pfd;
	Time owned_at = CurrentTime;

	display = XOpenDisplay(NULL);
	if (display == NULL)
		return 1;
	window = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, 1, 1, 0, 0, 0);
	clipboard = XInternAtom(display, "CLIPBOARD", False);
	utf8 = XInternAtom(display, "UTF8_STRING", False);
	targets = XInternAtom(display, "TARGETS", False);
	timestamp = XInternAtom(display, "TIMESTAMP", False);
	memset(&action, 0, sizeof(action));
	action.sa_handler = holder_handler;
	sigemptyset(&action.sa_mask);
	(void)sigaction(SIGTERM, &action, NULL);
	(void)sigaction(SIGINT, &action, NULL);
	XSetSelectionOwner(display, clipboard, window, CurrentTime);
	XFlush(display);
	if (XGetSelectionOwner(display, clipboard) != window) {
		XDestroyWindow(display, window);
		XCloseDisplay(display);
		return 1;
	}
	owned_at = XLastKnownRequestProcessed(display);
	if (write_pid_file(pid_path) < 0) {
		XDestroyWindow(display, window);
		XCloseDisplay(display);
		return 1;
	}
	if (ready_fd >= 0) {
		(void)sc_write_all(ready_fd, "1", 1);
		close(ready_fd);
	}
	while (!holder_stop_flag) {
		pfd.fd = ConnectionNumber(display);
		pfd.events = POLLIN;
		pfd.revents = 0;
		if (poll(&pfd, 1, 200) < 0 && errno != EINTR)
			break;
		while (XPending(display) != 0) {
			XNextEvent(display, &event);
			if (event.type == SelectionClear &&
			    event.xselectionclear.selection == clipboard) {
				holder_stop_flag = 1;
				break;
			}
			if (event.type != SelectionRequest ||
			    event.xselectionrequest.selection != clipboard)
				continue;
			{
				XSelectionEvent reply;
				Atom list[4];

				memset(&reply, 0, sizeof(reply));
				reply.type = SelectionNotify;
				reply.display = event.xselectionrequest.display;
				reply.requestor = event.xselectionrequest.requestor;
				reply.selection = event.xselectionrequest.selection;
				reply.target = event.xselectionrequest.target;
				reply.time = event.xselectionrequest.time;
				reply.property = None;
				if (event.xselectionrequest.target == targets) {
					list[0] = timestamp;
					list[1] = targets;
					list[2] = utf8;
					list[3] = XA_STRING;
					XChangeProperty(display, event.xselectionrequest.requestor,
					    event.xselectionrequest.property, XA_ATOM, 32,
					    PropModeReplace, (unsigned char *)list, 4);
					reply.property = event.xselectionrequest.property;
				} else if (event.xselectionrequest.target == timestamp) {
					XChangeProperty(display, event.xselectionrequest.requestor,
					    event.xselectionrequest.property, XA_INTEGER, 32,
					    PropModeReplace, (unsigned char *)&owned_at, 1);
					reply.property = event.xselectionrequest.property;
				} else if (event.xselectionrequest.target == utf8 ||
				    event.xselectionrequest.target == XA_STRING) {
					if (len > (size_t)INT_MAX)
						reply.property = None;
					else {
						XChangeProperty(display, event.xselectionrequest.requestor,
						    event.xselectionrequest.property,
						    event.xselectionrequest.target, 8, PropModeReplace,
						    (const unsigned char *)text, (int)len);
						reply.property = event.xselectionrequest.property;
					}
				} else {
					reply.property = None;
				}
				XSendEvent(display, event.xselectionrequest.requestor, False, 0,
				    (XEvent *)&reply);
				XFlush(display);
			}
		}
	}
	(void)unlink(pid_path);
	XDestroyWindow(display, window);
	XCloseDisplay(display);
	return 0;
}

static void
close_extra_fds(int keep)
{
	DIR *dir;
	struct dirent *entry;
	int fd;

	dir = opendir("/proc/self/fd");
	if (dir == NULL)
		return;
	while ((entry = readdir(dir)) != NULL) {
		char *end;
		long value;

		if (entry->d_name[0] == '.')
			continue;
		errno = 0;
		value = strtol(entry->d_name, &end, 10);
		if (errno != 0 || *end != '\0' || value < 0 || value > INT_MAX)
			continue;
		fd = (int)value;
		if (fd == STDIN_FILENO || fd == STDOUT_FILENO || fd == STDERR_FILENO ||
		    fd == keep || fd == dirfd(dir))
			continue;
		(void)close(fd);
	}
	closedir(dir);
}

/*
 * Fork holder, wait <=1s for the ownership handshake, then reply.
 * The middle child is reaped immediately so no zombie remains.
 */
static int
respond_execute(int fd, const char *id, const char *result_id)
{
	struct snippet_set set;
	const char *fail = NULL;
	const char *colon;
	char *end;
	unsigned long lineno;
	const char *want_title;
	const struct snippet *hit = NULL;
	size_t i;
	int ready[2] = { -1, -1 };
	pid_t middle;
	char pid_path[PATH_MAX];
	char *body = NULL;
	size_t body_len = 0;
	int status = 1;

	if (load_snippets(&set, &fail) < 0)
		return respond_error(fd, id, fail);
	colon = strchr(result_id, ':');
	if (colon == NULL || colon == result_id) {
		free_set(&set);
		return respond_error(fd, id, "unknown snippet");
	}
	errno = 0;
	lineno = strtoul(result_id, &end, 10);
	if (errno != 0 || end != colon || lineno == 0) {
		free_set(&set);
		return respond_error(fd, id, "unknown snippet");
	}
	want_title = colon + 1;
	for (i = 0; i < set.len; i++) {
		if (set.items[i].lineno == lineno && strcmp(set.items[i].title, want_title) == 0) {
			hit = &set.items[i];
			break;
		}
	}
	if (hit == NULL) {
		free_set(&set);
		return respond_error(fd, id, "unknown snippet");
	}
	body = sc_xstrndup(hit->body, strlen(hit->body));
	if (body == NULL) {
		free_set(&set);
		return respond_error(fd, id, "cannot own clipboard");
	}
	body_len = strlen(body);
	if (set.skipped != 0)
		fprintf(stderr, "snippets: skipped %lu bad lines\n", set.skipped);
	free_set(&set);
	if (holder_pid_path(pid_path, sizeof(pid_path)) < 0 || replace_holder() < 0 ||
	    pipe(ready) < 0) {
		free(body);
		return respond_error(fd, id, "cannot own clipboard");
	}
	(void)fcntl(ready[0], F_SETFD, FD_CLOEXEC);
	middle = fork();
	if (middle < 0) {
		close(ready[0]);
		close(ready[1]);
		free(body);
		return respond_error(fd, id, "cannot own clipboard");
	}
	if (middle == 0) {
		pid_t grand;

		close(ready[0]);
		grand = fork();
		if (grand < 0)
			_exit(1);
		if (grand != 0)
			_exit(0);
		(void)setsid();
		close(STDIN_FILENO);
		close(STDOUT_FILENO);
		close(STDERR_FILENO);
		close_extra_fds(ready[1]);
		(void)open("/dev/null", O_RDWR);
		(void)open("/dev/null", O_RDWR);
		(void)open("/dev/null", O_RDWR);
		status = holder_main(body, body_len, ready[1], pid_path);
		_exit(status);
	}
	close(ready[1]);
	free(body);
	for (;;) {
		int ret = waitpid(middle, &status, 0);

		if (ret == middle)
			break;
		if (ret < 0 && errno != EINTR)
			break;
	}
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		close(ready[0]);
		return respond_error(fd, id, "cannot own clipboard");
	}
	for (;;) {
		struct pollfd pfd;
		char byte;
		ssize_t n;

		pfd.fd = ready[0];
		pfd.events = POLLIN;
		pfd.revents = 0;
		if (poll(&pfd, 1, 1000) <= 0) {
			close(ready[0]);
			return respond_error(fd, id, "cannot own clipboard");
		}
		n = read(ready[0], &byte, 1);
		close(ready[0]);
		if (n == 1 && byte == '1') {
			const char *fields[] = { "OK", id };

			return write_fields(fd, fields, 2);
		}
		return respond_error(fd, id, "cannot own clipboard");
	}
}

static int
read_line(int fd, char **line, size_t *len)
{
	char *buf = NULL;
	char byte;
	size_t used = 0;
	ssize_t n;

	*line = NULL;
	*len = 0;
	for (;;) {
		n = read(fd, &byte, 1);
		if (n == 0)
			return used == 0 ? 0 : -1;
		if (n < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (used == SNIPPETS_RAW_MAX + 1) {
			free(buf);
			return -1;
		}
		{
			char *next = realloc(buf, used + 1);

			if (next == NULL) {
				free(buf);
				return -1;
			}
			buf = next;
		}
		buf[used++] = byte;
		if (byte == '\n') {
			*line = buf;
			*len = used;
			return 1;
		}
	}
}

static int
handle_stdin(int once)
{
	struct sc_record request;
	char *line;
	size_t len;
	int ret;

	while ((ret = read_line(STDIN_FILENO, &line, &len)) == 1) {
		if (sc_protocol_parse(line, len, &request) < 0) {
			free(line);
			return 1;
		}
		free(line);
		if (request.kind == SC_RECORD_QUIT) {
			sc_record_free(&request);
			return 0;
		}
		if (request.kind == SC_RECORD_QUERY)
			ret = respond_query(STDOUT_FILENO, request.fields[1], request.fields[2]);
		else if (request.kind == SC_RECORD_EXECUTE && request.nfields == 4)
			ret = respond_execute(STDOUT_FILENO, request.fields[1], request.fields[3]);
		else
			ret = respond_error(STDOUT_FILENO,
			    request.nfields > 1 ? request.fields[1] : "0", "unsupported snippets request");
		sc_record_free(&request);
		if (ret < 0)
			return 1;
		if (once)
			return 0;
	}
	return ret < 0 ? 1 : 0;
}

static int
stop_holder_cmd(void)
{
	char path[PATH_MAX];

	if (holder_pid_path(path, sizeof(path)) < 0) {
		fprintf(stderr, "snippets: cannot resolve holder pid file\n");
		return 1;
	}
	if (stop_recorded(path) < 0) {
		fprintf(stderr, "snippets: cannot stop holder\n");
		return 1;
	}
	return 0;
}

int
main(int argc, char **argv)
{
	if (argc != 2)
		return 2;
	if (strcmp(argv[1], "--superclip-describe") == 0) {
		fputs("SUPERCLIP\t1\tsnippets\tpersistent\tchange\tSnippets\n", stdout);
		return 0;
	}
	if (strcmp(argv[1], "--superclip-setup") == 0) {
		fputs("NONE\n", stdout);
		return 0;
	}
	if (strcmp(argv[1], "--superclip-session") == 0)
		return handle_stdin(0);
	if (strcmp(argv[1], "--superclip-query") == 0 || strcmp(argv[1], "--superclip-execute") == 0)
		return handle_stdin(1);
	if (strcmp(argv[1], "--holder-stop") == 0)
		return stop_holder_cmd();
	return 2;
}
