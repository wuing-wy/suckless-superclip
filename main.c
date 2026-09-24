#include "config.h"
#include "extension.h"
#include "text.h"
#include "util.h"
#include "x11.h"

#include <X11/keysym.h>

#include <errno.h>
#include <locale.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

enum app_state {
	APP_EXTENSION_SELECT,
	APP_QUERY_EDIT,
	APP_QUERY_RUNNING,
	APP_RESULT_SELECT,
	APP_ERROR
};

enum app_phase {
	APP_PHASE_NONE,
	APP_PHASE_DESCRIBE,
	APP_PHASE_QUERY,
	APP_PHASE_EXECUTE
};

struct app_result {
	char *id;
	char *title;
	char *description;
};

struct app {
	struct sc_x11 x11;
	struct sc_extensions extensions;
	struct sc_extension *extension;
	struct sc_text query;
	struct app_result results[SUPERCLIP_MAX_ITEMS];
	size_t nresults;
	size_t selected;
	unsigned long request_id;
	unsigned long active_id;
	int response_begun;
	long long debounce_at;
	enum app_state state;
	enum app_phase phase;
	struct sc_child child;
	int child_live;
	int running;
	char status[SUPERCLIP_MAX_STDERR + 1];
};

static long long
now_millis(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0)
		return 0;
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void
set_status(struct app *app, const char *message)
{
	size_t len;

	if (message == NULL)
		message = "";
	len = strlen(message);
	if (len > SUPERCLIP_MAX_STDERR)
		len = SUPERCLIP_MAX_STDERR;
	memcpy(app->status, message, len);
	app->status[len] = '\0';
}

static void
free_results(struct app *app)
{
	size_t i;

	for (i = 0; i < app->nresults; i++) {
		free(app->results[i].id);
		free(app->results[i].title);
		free(app->results[i].description);
	}
	app->nresults = 0;
	app->selected = 0;
}

static int
add_result(struct app *app, const char *id, const char *title, const char *description)
{
	struct app_result *result;

	if (app->nresults == SUPERCLIP_MAX_ITEMS)
		return 0;
	result = &app->results[app->nresults];
	result->id = sc_xstrndup(id, strlen(id));
	result->title = sc_xstrndup(title, strlen(title));
	result->description = sc_xstrndup(description, strlen(description));
	if (result->id == NULL || result->title == NULL || result->description == NULL) {
		free(result->id);
		free(result->title);
		free(result->description);
		memset(result, 0, sizeof(*result));
		return -1;
	}
	app->nresults++;
	return 0;
}

static void
close_child(struct app *app)
{
	if (app->child_live)
		sc_child_close(&app->child);
	app->child_live = 0;
	app->phase = APP_PHASE_NONE;
}

static int
open_child(struct app *app, const char *command)
{
	if (app->child_live)
		return -1;
	if (sc_child_spawn(&app->child, app->extension->path, command) < 0)
		return -1;
	app->child_live = 1;
	return 0;
}

static int
queue_query(struct app *app)
{
	char id[32];
	const char *fields[3];

	if (app->extension == NULL || app->phase != APP_PHASE_NONE)
		return -1;
	app->request_id++;
	app->active_id = app->request_id;
	app->response_begun = 0;
	(void)snprintf(id, sizeof(id), "%lu", app->active_id);
	fields[0] = "QUERY";
	fields[1] = id;
	fields[2] = app->query.data;
	free_results(app);
	set_status(app, "");
	app->state = APP_QUERY_RUNNING;
	app->phase = APP_PHASE_QUERY;
	if (app->extension->mode == SC_PROCESS_SHORT) {
		if (open_child(app, "--superclip-query") < 0)
			goto fail;
		if (sc_child_queue(&app->child, fields, 3, 1) < 0)
			goto fail;
	} else {
		if (!app->child_live && open_child(app, "--superclip-session") < 0)
			goto fail;
		if (sc_child_queue(&app->child, fields, 3, 0) < 0)
			goto fail;
	}
	return 0;
fail:
	set_status(app, "cannot start extension query");
	close_child(app);
	app->state = APP_ERROR;
	return -1;
}

static int
queue_execute(struct app *app)
{
	char id[32];
	const char *fields[4];

	if (app->extension == NULL || app->nresults == 0 || app->selected >= app->nresults ||
	    app->phase != APP_PHASE_NONE)
		return -1;
	app->request_id++;
	app->active_id = app->request_id;
	(void)snprintf(id, sizeof(id), "%lu", app->active_id);
	fields[0] = "EXECUTE";
	fields[1] = id;
	fields[2] = app->query.data;
	fields[3] = app->results[app->selected].id;
	app->phase = APP_PHASE_EXECUTE;
	set_status(app, "");
	if (app->extension->mode == SC_PROCESS_SHORT) {
		if (open_child(app, "--superclip-execute") < 0)
			goto fail;
		if (sc_child_queue(&app->child, fields, 4, 1) < 0)
			goto fail;
	} else if (sc_child_queue(&app->child, fields, 4, 0) < 0) {
		goto fail;
	}
	return 0;
fail:
	set_status(app, "cannot start extension action");
	close_child(app);
	app->state = APP_ERROR;
	return -1;
}

static int
select_extension(struct app *app, struct sc_extension *extension)
{
	if (extension == NULL)
		return -1;
	close_child(app);
	app->extension = extension;
	free_results(app);
	set_status(app, "");
	if (open_child(app, "--superclip-describe") < 0) {
		set_status(app, "cannot start extension description");
		app->state = APP_ERROR;
		return -1;
	}
	close(app->child.in_fd);
	app->child.in_fd = -1;
	app->child.input_closed = 1;
	app->phase = APP_PHASE_DESCRIBE;
	app->state = APP_QUERY_RUNNING;
	return 0;
}

static void
after_description(struct app *app)
{
	if (app->extension->mode == SC_PROCESS_PERSISTENT) {
		close_child(app);
		if (open_child(app, "--superclip-session") < 0) {
			set_status(app, "cannot start persistent extension");
			app->state = APP_ERROR;
			return;
		}
	}
	app->state = APP_QUERY_EDIT;
	app->phase = APP_PHASE_NONE;
	if (app->extension->mode == SC_PROCESS_PERSISTENT &&
	    app->extension->trigger == SC_TRIGGER_CHANGE)
		app->debounce_at = now_millis() + SUPERCLIP_DEBOUNCE_MS;
}

static void
handle_record(struct app *app, struct sc_record *record)
{
	unsigned long id;
	char *end;

	if (record->kind == SC_RECORD_ERROR) {
		set_status(app, record->fields[2]);
		app->state = APP_ERROR;
		if (app->extension != NULL && app->extension->mode == SC_PROCESS_SHORT)
			close_child(app);
		return;
	}
	if (app->phase == APP_PHASE_DESCRIBE) {
		if (sc_extension_apply_description(app->extension, record) < 0) {
			set_status(app, "invalid extension description");
			app->state = APP_ERROR;
			close_child(app);
			return;
		}
		after_description(app);
		return;
	}
	if (record->nfields < 2)
		return;
	errno = 0;
	id = strtoul(record->fields[1], &end, 10);
	if (errno != 0 || *end != '\0' || id != app->active_id)
		return;
	if (app->phase == APP_PHASE_QUERY) {
		if (record->kind == SC_RECORD_BEGIN && !app->response_begun) {
			free_results(app);
			app->response_begun = 1;
		} else if (record->kind == SC_RECORD_ITEM && app->response_begun) {
			if (add_result(app, record->fields[2], record->fields[3], record->fields[4]) < 0) {
				set_status(app, "out of memory while reading results");
				app->state = APP_ERROR;
			}
		} else if (record->kind == SC_RECORD_END && app->response_begun) {
			app->phase = APP_PHASE_NONE;
			app->response_begun = 0;
			app->state = APP_RESULT_SELECT;
			if (app->extension->mode == SC_PROCESS_SHORT)
				close_child(app);
		} else {
			set_status(app, "invalid extension response sequence");
			app->state = APP_ERROR;
			close_child(app);
		}
	} else if (app->phase == APP_PHASE_EXECUTE && record->kind == SC_RECORD_OK) {
		app->running = 0;
	}
}

static void
handle_child(struct app *app)
{
	struct sc_record record;
	int ret;

	if (!app->child_live)
		return;
	if (app->child.in_fd >= 0 && sc_child_flush(&app->child) < 0) {
		set_status(app, "extension input failed");
		app->state = APP_ERROR;
		close_child(app);
		return;
	}
	if (app->child.err_fd >= 0)
		(void)sc_child_drain_stderr(&app->child);
	while (app->child.out_fd >= 0) {
		ret = sc_child_read_record(&app->child, &record);
		if (ret == 1) {
			handle_record(app, &record);
			sc_record_free(&record);
			continue;
		}
		if (ret < 0) {
			set_status(app, "invalid extension protocol");
			app->state = APP_ERROR;
			close_child(app);
		}
		break;
	}
	if (app->child_live && sc_child_reap(&app->child, 0) < 0) {
		set_status(app, "extension wait failed");
		app->state = APP_ERROR;
		close_child(app);
	}
	if (app->child_live && app->child.exited && app->child.out_fd < 0 &&
	    app->phase != APP_PHASE_NONE) {
		if (app->child.stderr_buf != NULL && app->child.stderr_buf[0] != '\0')
			set_status(app, app->child.stderr_buf);
		else
			set_status(app, "extension exited without a complete response");
		app->state = APP_ERROR;
		close_child(app);
	}
}

static void
edited_query(struct app *app)
{
	free_results(app);
	if (app->extension == NULL) {
		app->state = APP_EXTENSION_SELECT;
		return;
	}
	if (app->phase == APP_PHASE_QUERY) {
		if (app->extension->mode == SC_PROCESS_SHORT)
			close_child(app);
		else {
			app->active_id = 0;
			app->response_begun = 0;
			app->phase = APP_PHASE_NONE;
		}
	}
	if (app->extension->mode == SC_PROCESS_PERSISTENT &&
	    app->extension->trigger == SC_TRIGGER_CHANGE)
		app->debounce_at = now_millis() + SUPERCLIP_DEBOUNCE_MS;
	app->state = APP_QUERY_EDIT;
}

static void
handle_key(struct app *app, XKeyEvent *event)
{
	KeySym keysym;
	char *text = NULL;
	int n;

	n = sc_x11_lookup(&app->x11, event, &text, &keysym);
	if (n < 0) {
		set_status(app, "invalid input");
		return;
	}
	if (keysym == XK_Escape) {
		app->running = 0;
		goto out;
	}
	if (keysym == XK_Up || (keysym == XK_p && (event->state & ControlMask))) {
		if (app->selected > 0)
			app->selected--;
		goto out;
	}
	if (keysym == XK_Down || (keysym == XK_n && (event->state & ControlMask))) {
		if ((app->extension == NULL && app->selected + 1 < app->extensions.len) ||
		    (app->extension != NULL && app->selected + 1 < app->nresults))
			app->selected++;
		goto out;
	}
	if (keysym == XK_Return || keysym == XK_KP_Enter) {
		if (app->extension == NULL) {
			if (app->selected < app->extensions.len)
				(void)select_extension(app, &app->extensions.items[app->selected]);
		} else if (app->state == APP_RESULT_SELECT && app->nresults != 0) {
			(void)queue_execute(app);
		} else {
			(void)queue_query(app);
		}
		goto out;
	}
	if (keysym == XK_BackSpace) {
		sc_text_backspace(&app->query);
		edited_query(app);
		goto out;
	}
	if (keysym == XK_Delete) {
		sc_text_delete(&app->query);
		edited_query(app);
		goto out;
	}
	if (keysym == XK_Left) {
		sc_text_left(&app->query);
		goto out;
	}
	if (keysym == XK_Right) {
		sc_text_right(&app->query);
		goto out;
	}
	if (keysym == XK_Home) {
		sc_text_home(&app->query);
		goto out;
	}
	if (keysym == XK_End) {
		sc_text_end(&app->query);
		goto out;
	}
	if (keysym == XK_v && (event->state & ControlMask)) {
		sc_x11_request_paste(&app->x11);
		goto out;
	}
	if (n > 0 && text != NULL && sc_text_insert(&app->query, text, (size_t)n, SUPERCLIP_MAX_QUERY) == 0)
		edited_query(app);
out:
	free(text);
}

static void
handle_x_events(struct app *app)
{
	XEvent event;
	char *paste;
	size_t len;

	while (XPending(app->x11.display) != 0) {
		XNextEvent(app->x11.display, &event);
		/* XIM consumes composition events before normal key handling. */
		if (XFilterEvent(&event, None))
			continue;
		if (event.type == KeyPress)
			handle_key(app, &event.xkey);
		else if (event.type == SelectionNotify &&
		    sc_x11_take_paste(&app->x11, &event.xselection, &paste, &len) == 0) {
			if (sc_text_insert(&app->query, paste, len, SUPERCLIP_MAX_QUERY) == 0)
				edited_query(app);
			free(paste);
		}
	}
}

static void
draw(struct app *app)
{
	struct sc_draw_item items[SUPERCLIP_MAX_ITEMS];
	char prompt_buf[512];
	const char *prompt = "> ";
	size_t i, n = 0, selected = app->selected;

	if (app->extension == NULL) {
		for (i = 0; i < app->extensions.len; i++) {
			items[i].title = app->extensions.items[i].name;
			items[i].description = "";
		}
		n = app->extensions.len;
	} else {
		(void)snprintf(prompt_buf, sizeof(prompt_buf), "%s › ", app->extension->name);
		prompt = prompt_buf;
		for (i = 0; i < app->nresults; i++) {
			items[i].title = app->results[i].title;
			items[i].description = app->results[i].description;
		}
		n = app->nresults;
	}
	if (n == 0)
		selected = 0;
	sc_x11_draw(&app->x11, prompt, app->query.data, app->query.cursor, items, n, selected, app->status);
}

static int
read_description(struct sc_extension *extension)
{
	struct sc_child child;
	struct sc_record record;
	struct pollfd pfd;
	int ret;
	int seen = 0;

	if (sc_child_spawn(&child, extension->path, "--superclip-describe") < 0)
		return -1;
	close(child.in_fd);
	child.in_fd = -1;
	child.input_closed = 1;
	for (;;) {
		ret = sc_child_read_record(&child, &record);
		if (ret == 1) {
			if (seen != 0 || sc_extension_apply_description(extension, &record) < 0) {
				sc_record_free(&record);
				sc_child_close(&child);
				return -1;
			}
			seen = 1;
			sc_record_free(&record);
			continue;
		}
		if (ret < 0)
			break;
		if (child.out_fd < 0)
			break;
		pfd.fd = child.out_fd;
		pfd.events = POLLIN | POLLHUP;
		pfd.revents = 0;
		if (poll(&pfd, 1, 1000) <= 0)
			break;
	}
	sc_child_close(&child);
	return seen != 0 && ret >= 0 ? 0 : -1;
}

static int
run_setup(struct sc_extensions *extensions)
{
	size_t i;
	struct sc_child child;
	struct sc_record record;
	struct pollfd pfd;
	int ret;

	for (i = 0; i < extensions->len; i++) {
		printf("%s:\n", extensions->items[i].name);
		if (read_description(&extensions->items[i]) < 0) {
			puts("  error: invalid description");
			continue;
		}
		if (sc_child_spawn(&child, extensions->items[i].path, "--superclip-setup") < 0) {
			puts("  error: cannot start");
			continue;
		}
		close(child.in_fd);
		child.in_fd = -1;
		child.input_closed = 1;
		for (;;) {
			ret = sc_child_read_record(&child, &record);
			if (ret == 1) {
				if (record.kind != SC_RECORD_NONE && record.kind != SC_RECORD_AUTOSTART) {
					sc_record_free(&record);
					puts("  error: invalid setup response");
					break;
				}
				size_t field;
				for (field = 0; field < record.nfields; field++)
					printf("%s%s", field == 0 ? "  " : "\t", record.fields[field]);
				putchar('\n');
				sc_record_free(&record);
				continue;
			}
			if (ret < 0) {
				puts("  error: invalid setup response");
				break;
			}
			if (child.out_fd < 0)
				break;
			pfd.fd = child.out_fd;
			pfd.events = POLLIN | POLLHUP;
			pfd.revents = 0;
			if (poll(&pfd, 1, 1000) <= 0)
				break;
		}
		sc_child_close(&child);
	}
	return 0;
}

static char *
default_user_extension_dir(void)
{
	const char *xdg = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");
	char *dir;
	int n;

	if (xdg != NULL && xdg[0] != '\0')
		n = snprintf(NULL, 0, "%s/superclip/extensions", xdg);
	else if (home != NULL && home[0] != '\0')
		n = snprintf(NULL, 0, "%s/.config/superclip/extensions", home);
	else
		return NULL;
	if (n < 0)
		return NULL;
	dir = malloc((size_t)n + 1);
	if (dir == NULL)
		return NULL;
	if (xdg != NULL && xdg[0] != '\0')
		(void)snprintf(dir, (size_t)n + 1, "%s/superclip/extensions", xdg);
	else
		(void)snprintf(dir, (size_t)n + 1, "%s/.config/superclip/extensions", home);
	return dir;
}

int
main(int argc, char **argv)
{
	struct app app;
	char *user_dir;
	char system_dir[1024];
	char *initial = NULL;
	size_t i, initial_len = 0, arglen, offset;
	int timeout;
	struct pollfd fds[4];
	int nfds;

	/* XIM needs the user's UTF-8 locale and XMODIFIERS before XOpenDisplay. */
	if (setlocale(LC_CTYPE, "") == NULL) {
		fprintf(stderr, "superclip: cannot set LC_CTYPE locale\n");
		return 1;
	}
	(void)XSetLocaleModifiers("");
	memset(&app, 0, sizeof(app));
	app.child.in_fd = app.child.out_fd = app.child.err_fd = -1;
	user_dir = default_user_extension_dir();
	(void)snprintf(system_dir, sizeof(system_dir), "%s/libexec/superclip/extensions", SUPERCLIP_PREFIX);
	if (sc_extensions_discover(&app.extensions, user_dir, system_dir) < 0) {
		fprintf(stderr, "superclip: cannot discover extensions\n");
		free(user_dir);
		return 1;
	}
	free(user_dir);
	if (argc > 1 && strcmp(argv[1], "setup") == 0) {
		int ret = run_setup(&app.extensions);
		sc_extensions_free(&app.extensions);
		return ret;
	}
	if (argc > 1) {
		app.extension = sc_extensions_find(&app.extensions, argv[1]);
		if (app.extension == NULL) {
			fprintf(stderr, "superclip: unknown extension: %s\n", argv[1]);
			sc_extensions_free(&app.extensions);
			return 1;
		}
		for (i = 2; i < (size_t)argc; i++) {
			arglen = strlen(argv[i]);
			if (arglen > SUPERCLIP_MAX_QUERY - initial_len - (i == 2 ? 0U : 1U)) {
				fprintf(stderr, "superclip: initial query is too long\n");
				sc_extensions_free(&app.extensions);
				return 1;
			}
			initial_len += arglen + (i == 2 ? 0U : 1U);
		}
		initial = malloc(initial_len + 1);
		if (initial == NULL) {
			sc_extensions_free(&app.extensions);
			return 1;
		}
		offset = 0;
		for (i = 2; i < (size_t)argc; i++) {
			if (i != 2)
				initial[offset++] = ' ';
			arglen = strlen(argv[i]);
			memcpy(initial + offset, argv[i], arglen);
			offset += arglen;
		}
		initial[offset] = '\0';
	}
	if (sc_text_init(&app.query, SUPERCLIP_MAX_QUERY) < 0 ||
	    sc_text_set(&app.query, initial == NULL ? "" : initial, initial_len, SUPERCLIP_MAX_QUERY) < 0 ||
	    sc_x11_open(&app.x11) < 0) {
		fprintf(stderr, "superclip: cannot initialize X11 frontend\n");
		free(initial);
		sc_text_free(&app.query);
		sc_extensions_free(&app.extensions);
		return 1;
	}
	free(initial);
	app.running = 1;
	app.state = app.extension == NULL ? APP_EXTENSION_SELECT : APP_QUERY_EDIT;
	if (app.extension != NULL)
		(void)select_extension(&app, app.extension);
	while (app.running) {
		nfds = 0;
		fds[nfds].fd = sc_x11_fd(&app.x11);
		fds[nfds].events = POLLIN;
		fds[nfds++].revents = 0;
		if (app.child_live && app.child.out_fd >= 0) {
			fds[nfds].fd = app.child.out_fd;
			fds[nfds].events = POLLIN | POLLHUP;
			fds[nfds++].revents = 0;
		}
		if (app.child_live && app.child.err_fd >= 0) {
			fds[nfds].fd = app.child.err_fd;
			fds[nfds].events = POLLIN | POLLHUP;
			fds[nfds++].revents = 0;
		}
		if (app.child_live && app.child.in_fd >= 0 && app.child.write_len != app.child.write_off) {
			fds[nfds].fd = app.child.in_fd;
			fds[nfds].events = POLLOUT;
			fds[nfds++].revents = 0;
		}
		timeout = -1;
		if (app.debounce_at != 0) {
			long long wait = app.debounce_at - now_millis();
			timeout = wait <= 0 ? 0 : (wait > INT32_MAX ? INT32_MAX : (int)wait);
		}
		(void)poll(fds, (nfds_t)nfds, timeout);
		handle_x_events(&app);
		handle_child(&app);
		if (app.debounce_at != 0 && now_millis() >= app.debounce_at) {
			app.debounce_at = 0;
			if (app.extension != NULL && app.extension->mode == SC_PROCESS_PERSISTENT &&
			    app.extension->trigger == SC_TRIGGER_CHANGE && app.phase == APP_PHASE_NONE)
				(void)queue_query(&app);
		}
		draw(&app);
	}
	close_child(&app);
	free_results(&app);
	sc_x11_close(&app.x11);
	sc_text_free(&app.query);
	sc_extensions_free(&app.extensions);
	return 0;
}
