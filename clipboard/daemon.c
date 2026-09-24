#include "clipboard/store.h"
#include "protocol.h"
#include "util.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xfixes.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

struct daemon {
	Display *display;
	Window window;
	Atom clipboard;
	Atom utf8;
	Atom targets;
	Atom property;
	Atom incr;
	int xfixes_event;
	int listen_fd;
	int lock_fd;
	int owns_socket;
	char socket_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
	char lock_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
	struct sc_clip_store store;
	char *owned;
	size_t owned_len;
	char *incoming;
	size_t incoming_len;
	int receiving;
	int trying_string;
};

static volatile sig_atomic_t stopping;

static void
stop_handler(int signal_number)
{
	(void)signal_number;
	stopping = 1;
}

static int
install_signal_handlers(void)
{
	struct sigaction action;

	memset(&action, 0, sizeof(action));
	action.sa_handler = stop_handler;
	sigemptyset(&action.sa_mask);
	if (sigaction(SIGTERM, &action, NULL) < 0 ||
	    sigaction(SIGINT, &action, NULL) < 0 ||
	    sigaction(SIGHUP, &action, NULL) < 0)
		return -1;
	action.sa_handler = SIG_IGN;
	return sigaction(SIGPIPE, &action, NULL);
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
paths(struct daemon *daemon, char **state_path)
{
	const char *runtime = getenv("XDG_RUNTIME_DIR");
	const char *state = getenv("XDG_STATE_HOME");
	const char *home = getenv("HOME");
	char base[PATH_MAX];
	int n;

	if (runtime != NULL && runtime[0] != '\0')
		n = snprintf(base, sizeof(base), "%s/superclip", runtime);
	else
		n = snprintf(base, sizeof(base), "/tmp/superclip-%lu", (unsigned long)getuid());
	if (n < 0 || (size_t)n >= sizeof(base) || mkdir_private(base) < 0)
		return -1;
	n = snprintf(daemon->socket_path, sizeof(daemon->socket_path), "%s/clipboard.sock", base);
	if (n < 0 || (size_t)n >= sizeof(daemon->socket_path))
		return -1;
	n = snprintf(daemon->lock_path, sizeof(daemon->lock_path), "%s/clipboard.lock", base);
	if (n < 0 || (size_t)n >= sizeof(daemon->lock_path))
		return -1;
	if (state != NULL && state[0] != '\0')
		n = snprintf(base, sizeof(base), "%s/superclip", state);
	else if (home != NULL && home[0] != '\0')
		n = snprintf(base, sizeof(base), "%s/.local/state/superclip", home);
	else
		return -1;
	if (n < 0 || (size_t)n >= sizeof(base) || mkdir_private(base) < 0)
		return -1;
	n = snprintf(NULL, 0, "%s/clipboard", base);
	if (n < 0)
		return -1;
	*state_path = malloc((size_t)n + 1);
	if (*state_path == NULL)
		return -1;
	(void)snprintf(*state_path, (size_t)n + 1, "%s/clipboard", base);
	return 0;
}

static int
open_lock(struct daemon *daemon)
{
	struct flock lock;
	struct stat st;

	daemon->lock_fd = open(daemon->lock_path,
	    O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (daemon->lock_fd < 0) {
		fprintf(stderr, "superclip-clipboardd: cannot open instance lock: %s\n",
		    strerror(errno));
		return -1;
	}
	if (fstat(daemon->lock_fd, &st) < 0 || !S_ISREG(st.st_mode) ||
	    st.st_uid != getuid() || fchmod(daemon->lock_fd, 0600) < 0) {
		fprintf(stderr, "superclip-clipboardd: unsafe instance lock\n");
		return -1;
	}
	memset(&lock, 0, sizeof(lock));
	lock.l_type = F_WRLCK;
	lock.l_whence = SEEK_SET;
	if (fcntl(daemon->lock_fd, F_SETLK, &lock) < 0) {
		if (errno == EACCES || errno == EAGAIN)
			fprintf(stderr, "superclip-clipboardd: already running\n");
		else
			fprintf(stderr, "superclip-clipboardd: cannot lock instance: %s\n",
			    strerror(errno));
		return -1;
	}
	return 0;
}

/* Returns 1 for a live listener, 0 for a stale path, and -1 when uncertain. */
static int
socket_is_live(const char *path)
{
	struct sockaddr_un address;
	int fd, saved_errno;

	fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return -1;
	memset(&address, 0, sizeof(address));
	address.sun_family = AF_UNIX;
	memcpy(address.sun_path, path, strlen(path) + 1);
	if (connect(fd, (struct sockaddr *)&address, sizeof(address)) == 0) {
		close(fd);
		return 1;
	}
	saved_errno = errno;
	close(fd);
	if (saved_errno == ECONNREFUSED || saved_errno == ENOENT)
		return 0;
	errno = saved_errno;
	return -1;
}

static int
remove_stale_socket(struct daemon *daemon)
{
	struct stat st;
	int live;

	live = socket_is_live(daemon->socket_path);
	if (live > 0) {
		fprintf(stderr, "superclip-clipboardd: already running\n");
		return -1;
	}
	if (live < 0) {
		fprintf(stderr, "superclip-clipboardd: cannot inspect existing socket: %s\n",
		    strerror(errno));
		return -1;
	}
	if (lstat(daemon->socket_path, &st) < 0) {
		if (errno == ENOENT)
			return 0;
		fprintf(stderr, "superclip-clipboardd: cannot inspect stale socket: %s\n",
		    strerror(errno));
		return -1;
	}
	if (!S_ISSOCK(st.st_mode) || st.st_uid != getuid()) {
		fprintf(stderr, "superclip-clipboardd: refusing to remove unsafe socket path\n");
		return -1;
	}
	if (unlink(daemon->socket_path) < 0 && errno != ENOENT) {
		fprintf(stderr, "superclip-clipboardd: cannot remove stale socket: %s\n",
		    strerror(errno));
		return -1;
	}
	return 0;
}

static int
open_socket(struct daemon *daemon)
{
	struct sockaddr_un address;

	daemon->listen_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (daemon->listen_fd < 0) {
		fprintf(stderr, "superclip-clipboardd: cannot create socket: %s\n",
		    strerror(errno));
		return -1;
	}
	memset(&address, 0, sizeof(address));
	address.sun_family = AF_UNIX;
	memcpy(address.sun_path, daemon->socket_path, strlen(daemon->socket_path) + 1);
	if (bind(daemon->listen_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
		if (errno != EADDRINUSE) {
			fprintf(stderr, "superclip-clipboardd: cannot bind socket: %s\n",
			    strerror(errno));
			return -1;
		}
		if (remove_stale_socket(daemon) < 0)
			return -1;
		if (bind(daemon->listen_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
			fprintf(stderr, "superclip-clipboardd: cannot replace stale socket: %s\n",
			    strerror(errno));
			return -1;
		}
	}
	daemon->owns_socket = 1;
	if (chmod(daemon->socket_path, 0600) < 0 || listen(daemon->listen_fd, 8) < 0) {
		fprintf(stderr, "superclip-clipboardd: cannot prepare socket: %s\n",
		    strerror(errno));
		return -1;
	}
	return 0;
}

static void
cleanup_daemon(struct daemon *daemon)
{
	if (daemon->listen_fd >= 0)
		close(daemon->listen_fd);
	if (daemon->owns_socket)
		(void)unlink(daemon->socket_path);
	if (daemon->lock_fd >= 0)
		close(daemon->lock_fd);
	free(daemon->owned);
	free(daemon->incoming);
	if (daemon->display != NULL)
		XCloseDisplay(daemon->display);
	sc_clip_store_close(&daemon->store);
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

static size_t
snippet_len(const char *text, size_t len)
{
	size_t n = len > 4096 ? 4096 : len;

	while (n != 0 && !sc_utf8_valid(text, n))
		n--;
	return n;
}

static int
respond_query(struct daemon *daemon, int fd, const char *id, const char *query)
{
	const char *fields[5];
	char idbuf[32];
	char *snippet;
	size_t i, n;

	fields[0] = "BEGIN";
	fields[1] = id;
	if (write_fields(fd, fields, 2) < 0)
		return -1;
	for (i = 0; i < daemon->store.len; i++) {
		const struct sc_clip_entry *entry = &daemon->store.entries[i];
		if (query[0] != '\0' && strstr(entry->text, query) == NULL)
			continue;
		n = snippet_len(entry->text, entry->len);
		snippet = sc_xstrndup(entry->text, n);
		if (snippet == NULL)
			return -1;
		(void)snprintf(idbuf, sizeof(idbuf), "%llu", (unsigned long long)entry->id);
		fields[0] = "ITEM";
		fields[1] = id;
		fields[2] = idbuf;
		fields[3] = snippet;
		fields[4] = "";
		if (write_fields(fd, fields, 5) < 0) {
			free(snippet);
			return -1;
		}
		free(snippet);
	}
	fields[0] = "END";
	fields[1] = id;
	return write_fields(fd, fields, 2);
}

static void
own_text(struct daemon *daemon, const char *text, size_t len)
{
	char *next;

	next = sc_xstrndup(text, len);
	if (next == NULL)
		return;
	free(daemon->owned);
	daemon->owned = next;
	daemon->owned_len = len;
	XSetSelectionOwner(daemon->display, daemon->clipboard, daemon->window, CurrentTime);
	XFlush(daemon->display);
}

static int
respond_execute(struct daemon *daemon, int fd, const char *id, const char *entry_id)
{
	const char *fields[3];
	char *end;
	unsigned long long parsed;
	const struct sc_clip_entry *entry;

	errno = 0;
	parsed = strtoull(entry_id, &end, 10);
	entry = errno == 0 && *end == '\0' ? sc_clip_store_find(&daemon->store, parsed) : NULL;
	if (entry == NULL) {
		fields[0] = "ERROR"; fields[1] = id; fields[2] = "clipboard entry no longer exists";
		return write_fields(fd, fields, 3);
	}
	if (sc_clip_store_touch(&daemon->store, entry->id) < 0) {
		fields[0] = "ERROR"; fields[1] = id; fields[2] = "clipboard entry no longer exists";
		return write_fields(fd, fields, 3);
	}
	entry = &daemon->store.entries[0];
	own_text(daemon, entry->text, entry->len);
	fields[0] = "OK"; fields[1] = id;
	return write_fields(fd, fields, 2);
}

static int
read_client_line(int fd, char **line, size_t *len)
{
	char *buf = NULL;
	char byte;
	ssize_t n;
	size_t used = 0;
	struct pollfd pfd;

	for (;;) {
		n = read(fd, &byte, 1);
		if (n == 0) {
			free(buf);
			return 0;
		}
		if (n < 0) {
			if (errno == EINTR)
				continue;
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				pfd.fd = fd;
				pfd.events = POLLIN;
				pfd.revents = 0;
				if (poll(&pfd, 1, 1000) > 0)
					continue;
			}
			free(buf);
			return -1;
		}
		if (used == 65536U) {
			free(buf);
			return -1;
		}
		{
			char *next = realloc(buf, used + 2);
			if (next == NULL) {
				free(buf);
				return -1;
			}
			buf = next;
		}
		buf[used++] = byte;
		if (byte == '\n') {
			buf[used] = '\0';
			*line = buf;
			*len = used;
			return 1;
		}
	}
}

static void
handle_client(struct daemon *daemon)
{
	int fd;
	char *line = NULL;
	size_t len;
	struct sc_record record;
	const char *fields[3];

	fd = accept(daemon->listen_fd, NULL, NULL);
	if (fd < 0)
		return;
	if (sc_set_nonblock(fd) < 0) {
		close(fd);
		return;
	}
	if (read_client_line(fd, &line, &len) != 1) {
		close(fd);
		return;
	}
	if (len >= 8 && memcmp(line, "CONTROL\t", 8) == 0) {
		if (strcmp(line + 8, "status\n") == 0) {
			char count[32];
			(void)snprintf(count, sizeof(count), "%zu", daemon->store.len);
			(void)dprintf(fd, "ok\t%s\t%s\n", count, daemon->store.path);
		} else if (strcmp(line + 8, "clear\n") == 0) {
			(void)dprintf(fd, "%s\n", sc_clip_store_clear(&daemon->store) == 0 ? "ok" : "error");
		}
		free(line);
		close(fd);
		return;
	}
	if (sc_protocol_parse(line, len, &record) == 0) {
		if (record.kind == SC_RECORD_QUERY)
			(void)respond_query(daemon, fd, record.fields[1], record.fields[2]);
		else if (record.kind == SC_RECORD_EXECUTE)
			(void)respond_execute(daemon, fd, record.fields[1], record.fields[3]);
		else {
			fields[0] = "ERROR"; fields[1] = record.nfields > 1 ? record.fields[1] : "0";
			fields[2] = "unsupported clipboard request";
			(void)write_fields(fd, fields, 3);
		}
		sc_record_free(&record);
	}
	free(line);
	close(fd);
}

static void
finish_incoming(struct daemon *daemon)
{
	if (daemon->incoming != NULL && daemon->incoming_len != 0)
		(void)sc_clip_store_add(&daemon->store, daemon->incoming, daemon->incoming_len);
	free(daemon->incoming);
	daemon->incoming = NULL;
	daemon->incoming_len = 0;
	daemon->receiving = 0;
}

static void
discard_incoming(struct daemon *daemon)
{
	free(daemon->incoming);
	daemon->incoming = NULL;
	daemon->incoming_len = 0;
	daemon->receiving = 0;
}

static void
read_selection(struct daemon *daemon)
{
	Atom type = None;
	int format;
	unsigned long nitems, after;
	unsigned char *data = NULL;
	int status;

	status = XGetWindowProperty(daemon->display, daemon->window, daemon->property, 0,
	    SC_CLIP_MAX_ENTRY / 4, False, AnyPropertyType, &type, &format, &nitems, &after, &data);
	if (status != Success || after != 0) {
		if (data != NULL)
			XFree(data);
		if (type == None && !daemon->trying_string) {
			daemon->trying_string = 1;
			XConvertSelection(daemon->display, daemon->clipboard, XA_STRING,
			    daemon->property, daemon->window, CurrentTime);
			XFlush(daemon->display);
		}
		return;
	}
	if (type == daemon->incr) {
		if (format != 32 || nitems == 0) {
			if (data != NULL)
				XFree(data);
			discard_incoming(daemon);
			return;
		}
		XFree(data);
		free(daemon->incoming);
		daemon->incoming = NULL;
		daemon->incoming_len = 0;
		daemon->receiving = 1;
		daemon->trying_string = 0;
		XDeleteProperty(daemon->display, daemon->window, daemon->property);
		return;
	}
	if (format != 8) {
		if (data != NULL)
			XFree(data);
		if (type == None && !daemon->trying_string) {
			daemon->trying_string = 1;
			XConvertSelection(daemon->display, daemon->clipboard, XA_STRING,
			    daemon->property, daemon->window, CurrentTime);
			XFlush(daemon->display);
		}
		return;
	}
	if (data != NULL && (type == daemon->utf8 || type == XA_STRING) && nitems <= SC_CLIP_MAX_ENTRY)
		(void)sc_clip_store_add(&daemon->store, (const char *)data, nitems);
	if (data != NULL)
		XFree(data);
	daemon->trying_string = 0;
}

static void
read_incr_chunk(struct daemon *daemon)
{
	Atom type = None;
	int format;
	unsigned long nitems, after;
	unsigned char *data = NULL;
	char *next;
	int status;

	if (!daemon->receiving)
		return;
	status = XGetWindowProperty(daemon->display, daemon->window, daemon->property, 0,
	    SC_CLIP_MAX_ENTRY / 4, False, AnyPropertyType, &type, &format, &nitems, &after, &data);
	if (status == Success && type == None && daemon->incoming_len != 0) {
		finish_incoming(daemon);
		return;
	}
	if (status != Success ||
	    format != 8 || after != 0 || (type != daemon->utf8 && type != XA_STRING)) {
		if (data != NULL)
			XFree(data);
		XDeleteProperty(daemon->display, daemon->window, daemon->property);
		discard_incoming(daemon);
		return;
	}
	if (nitems == 0) {
		XFree(data);
		XDeleteProperty(daemon->display, daemon->window, daemon->property);
		finish_incoming(daemon);
		return;
	}
	if (nitems > SC_CLIP_MAX_ENTRY - daemon->incoming_len) {
		XFree(data);
		XDeleteProperty(daemon->display, daemon->window, daemon->property);
		discard_incoming(daemon);
		return;
	}
	next = realloc(daemon->incoming, daemon->incoming_len + nitems);
	if (next == NULL) {
		XFree(data);
		XDeleteProperty(daemon->display, daemon->window, daemon->property);
		discard_incoming(daemon);
		return;
	}
	daemon->incoming = next;
	memcpy(daemon->incoming + daemon->incoming_len, data, nitems);
	daemon->incoming_len += nitems;
	XFree(data);
	XDeleteProperty(daemon->display, daemon->window, daemon->property);
}

static void
handle_selection_request(struct daemon *daemon, XSelectionRequestEvent *request)
{
	XSelectionEvent reply;
	Atom list[3];

	memset(&reply, 0, sizeof(reply));
	reply.type = SelectionNotify;
	reply.display = request->display;
	reply.requestor = request->requestor;
	reply.selection = request->selection;
	reply.target = request->target;
	reply.time = request->time;
	reply.property = None;
	if (daemon->owned != NULL && request->selection == daemon->clipboard) {
		if (request->target == daemon->targets) {
			list[0] = daemon->targets; list[1] = daemon->utf8; list[2] = XA_STRING;
			XChangeProperty(daemon->display, request->requestor, request->property, XA_ATOM, 32,
			    PropModeReplace, (unsigned char *)list, 3);
			reply.property = request->property;
		} else if (request->target == daemon->utf8 || request->target == XA_STRING) {
			XChangeProperty(daemon->display, request->requestor, request->property, request->target, 8,
			    PropModeReplace, (unsigned char *)daemon->owned, (int)daemon->owned_len);
			reply.property = request->property;
		}
	}
	XSendEvent(daemon->display, request->requestor, False, 0, (XEvent *)&reply);
	XFlush(daemon->display);
}

static void
handle_x(struct daemon *daemon)
{
	XEvent event;
	XFixesSelectionNotifyEvent *notify;

	while (XPending(daemon->display) != 0) {
		XNextEvent(daemon->display, &event);
		if (event.type == daemon->xfixes_event + XFixesSelectionNotify) {
			notify = (XFixesSelectionNotifyEvent *)&event;
			if (notify->selection == daemon->clipboard && notify->owner != daemon->window) {
				daemon->trying_string = 0;
				XConvertSelection(daemon->display, daemon->clipboard, daemon->utf8,
				    daemon->property, daemon->window, CurrentTime);
			}
		} else if (event.type == SelectionNotify && event.xselection.selection == daemon->clipboard) {
			read_selection(daemon);
		} else if (event.type == PropertyNotify && event.xproperty.atom == daemon->property &&
		    event.xproperty.state == PropertyNewValue) {
			read_incr_chunk(daemon);
		} else if (event.type == SelectionRequest) {
			handle_selection_request(daemon, &event.xselectionrequest);
		}
	}
}

static int
control_command(const char *command)
{
	struct sockaddr_un address;
	char path[sizeof(address.sun_path)];
	char buffer[4096];
	const char *runtime = getenv("XDG_RUNTIME_DIR");
	int fd;
	ssize_t n;

	if (runtime != NULL && runtime[0] != '\0') {
		(void)snprintf(path, sizeof(path), "%s/superclip/clipboard.sock", runtime);
	} else {
		(void)snprintf(path, sizeof(path), "/tmp/superclip-%lu/clipboard.sock",
		    (unsigned long)getuid());
	}
	fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return 1;
	memset(&address, 0, sizeof(address));
	address.sun_family = AF_UNIX;
	memcpy(address.sun_path, path, strlen(path) + 1);
	if (connect(fd, (struct sockaddr *)&address, sizeof(address)) < 0 ||
	    dprintf(fd, "CONTROL\t%s\n", command) < 0) {
		close(fd);
		return 1;
	}
	n = read(fd, buffer, sizeof(buffer) - 1);
	if (n > 0) {
		buffer[n] = '\0';
		fputs(buffer, stdout);
	}
	close(fd);
	return n > 0 && strncmp(buffer, "error", 5) != 0 ? 0 : 1;
}

int
main(int argc, char **argv)
{
	struct daemon daemon;
	char *state_path = NULL;
	struct pollfd fds[2];
	int xfixes_error, xfixes_major, xfixes_minor;
	int exit_status = 1;

	if (argc == 2 && (strcmp(argv[1], "status") == 0 || strcmp(argv[1], "clear") == 0))
		return control_command(argv[1]);
	if (argc != 1) {
		fprintf(stderr, "usage: superclip-clipboardd [status|clear]\n");
		return 2;
	}
	memset(&daemon, 0, sizeof(daemon));
	daemon.listen_fd = daemon.lock_fd = -1;
	if (install_signal_handlers() < 0) {
		fprintf(stderr, "superclip-clipboardd: cannot install signal handlers: %s\n",
		    strerror(errno));
		goto fail;
	}
	if (paths(&daemon, &state_path) < 0) {
		fprintf(stderr, "superclip-clipboardd: cannot prepare private paths\n");
		goto fail;
	}
	if (open_lock(&daemon) < 0)
		goto fail;
	if (sc_clip_store_open(&daemon.store, state_path) < 0) {
		fprintf(stderr, "superclip-clipboardd: cannot open history state\n");
		goto fail;
	}
	if ((daemon.display = XOpenDisplay(NULL)) == NULL) {
		fprintf(stderr, "superclip-clipboardd: cannot open X display\n");
		goto fail;
	}
	free(state_path);
	state_path = NULL;
	if (!XFixesQueryExtension(daemon.display, &daemon.xfixes_event, &xfixes_error) ||
	    !XFixesQueryVersion(daemon.display, &xfixes_major, &xfixes_minor)) {
		fprintf(stderr, "superclip-clipboardd: XFixes is unavailable\n");
		goto fail;
	}
	daemon.window = XCreateSimpleWindow(daemon.display, DefaultRootWindow(daemon.display), -1, -1, 1, 1, 0, 0, 0);
	XSelectInput(daemon.display, daemon.window, PropertyChangeMask);
	daemon.clipboard = XInternAtom(daemon.display, "CLIPBOARD", False);
	daemon.utf8 = XInternAtom(daemon.display, "UTF8_STRING", False);
	daemon.targets = XInternAtom(daemon.display, "TARGETS", False);
	daemon.property = XInternAtom(daemon.display, "SUPERCLIP_CLIPBOARD", False);
	daemon.incr = XInternAtom(daemon.display, "INCR", False);
	XFixesSelectSelectionInput(daemon.display, daemon.window, daemon.clipboard,
	    XFixesSetSelectionOwnerNotifyMask | XFixesSelectionWindowDestroyNotifyMask |
	    XFixesSelectionClientCloseNotifyMask);
	XConvertSelection(daemon.display, daemon.clipboard, daemon.utf8, daemon.property, daemon.window, CurrentTime);
	XFlush(daemon.display);
	if (open_socket(&daemon) < 0)
		goto fail;
	exit_status = 0;
	while (!stopping) {
		fds[0].fd = ConnectionNumber(daemon.display); fds[0].events = POLLIN; fds[0].revents = 0;
		fds[1].fd = daemon.listen_fd; fds[1].events = POLLIN; fds[1].revents = 0;
		if (poll(fds, 2, -1) < 0) {
			if (errno == EINTR && !stopping)
				continue;
			if (errno != EINTR) {
				fprintf(stderr, "superclip-clipboardd: poll failed: %s\n",
				    strerror(errno));
				exit_status = 1;
			}
			break;
		}
		if (fds[0].revents != 0)
			handle_x(&daemon);
		if (fds[1].revents != 0)
			handle_client(&daemon);
	}
fail:
	free(state_path);
	cleanup_daemon(&daemon);
	return exit_status;
}
