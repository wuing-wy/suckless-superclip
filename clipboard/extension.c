#include "protocol.h"
#include "util.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static int
socket_path(char path[sizeof(((struct sockaddr_un *)0)->sun_path)])
{
	const char *runtime = getenv("XDG_RUNTIME_DIR");
	int n;

	if (runtime != NULL && runtime[0] != '\0')
		n = snprintf(path, sizeof(((struct sockaddr_un *)0)->sun_path),
		    "%s/superclip/clipboard.sock", runtime);
	else
		n = snprintf(path, sizeof(((struct sockaddr_un *)0)->sun_path),
		    "/tmp/superclip-%lu/clipboard.sock", (unsigned long)getuid());
	return n > 0 && (size_t)n < sizeof(((struct sockaddr_un *)0)->sun_path) ? 0 : -1;
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
		if (used == 65536U) {
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
forward_record(const struct sc_record *request)
{
	struct sockaddr_un address;
	struct sc_record response;
	char path[sizeof(address.sun_path)];
	char *wire = NULL;
	char *line = NULL;
	size_t wire_len, line_len;
	int fd, ret;

	if (socket_path(path) < 0)
		return -1;
	fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return -1;
	memset(&address, 0, sizeof(address));
	address.sun_family = AF_UNIX;
	memcpy(address.sun_path, path, strlen(path) + 1);
	if (connect(fd, (struct sockaddr *)&address, sizeof(address)) < 0)
		goto fail;
	if (sc_protocol_serialize((const char *const *)request->fields, request->nfields, &wire, &wire_len) < 0 ||
	    sc_write_all(fd, wire, wire_len) < 0)
		goto fail;
	free(wire);
	wire = NULL;
	shutdown(fd, SHUT_WR);
	while ((ret = read_line(fd, &line, &line_len)) == 1) {
		if (sc_protocol_parse(line, line_len, &response) < 0)
			goto fail;
		sc_record_free(&response);
		if (sc_write_all(STDOUT_FILENO, line, line_len) < 0)
			goto fail;
		free(line);
		line = NULL;
	}
	close(fd);
	return ret == 0 ? 0 : -1;
fail:
	free(wire);
	free(line);
	close(fd);
	return -1;
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
		if ((request.kind != SC_RECORD_QUERY && request.kind != SC_RECORD_EXECUTE) ||
		    forward_record(&request) < 0) {
			const char *fields[] = { "ERROR", request.nfields > 1 ? request.fields[1] : "0",
			    "clipboard daemon is unavailable" };
			char *wire;
			size_t wire_len;
			if (sc_protocol_serialize(fields, 3, &wire, &wire_len) == 0) {
				(void)sc_write_all(STDOUT_FILENO, wire, wire_len);
				free(wire);
			}
		}
		sc_record_free(&request);
		if (once)
			return 0;
	}
	return ret < 0 ? 1 : 0;
}

int
main(int argc, char **argv)
{
	if (argc != 2)
		return 2;
	if (strcmp(argv[1], "--superclip-describe") == 0) {
		fputs("SUPERCLIP\t1\tclipboard\tpersistent\tchange\tClipboard history\n", stdout);
		return 0;
	}
	if (strcmp(argv[1], "--superclip-setup") == 0) {
		fputs("AUTOSTART\tClipboard daemon\tsuperclip-clipboardd &\tStart from .xinitrc or dwm autostart\n", stdout);
		return 0;
	}
	if (strcmp(argv[1], "--superclip-session") == 0)
		return handle_stdin(0);
	if (strcmp(argv[1], "--superclip-query") == 0 || strcmp(argv[1], "--superclip-execute") == 0)
		return handle_stdin(1);
	return 2;
}
