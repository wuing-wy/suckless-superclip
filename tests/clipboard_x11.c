#include <X11/Xatom.h>
#include <X11/Xlib.h>

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static long long
milliseconds(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0)
		return 0;
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int
wait_x(Display *display, int timeout)
{
	struct pollfd pfd;

	pfd.fd = ConnectionNumber(display);
	pfd.events = POLLIN;
	pfd.revents = 0;
	return poll(&pfd, 1, timeout);
}

static int
owner(Display *display, const char *text, int incremental)
{
	Window window;
	Atom clipboard, utf8, targets, incr;
	Window incr_requestor = None;
	Atom incr_property = None;
	size_t incr_offset = 0;
	XEvent event;
	long long deadline = milliseconds() + 5000;

	window = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, 1, 1, 0, 0, 0);
	clipboard = XInternAtom(display, "CLIPBOARD", False);
	utf8 = XInternAtom(display, "UTF8_STRING", False);
	targets = XInternAtom(display, "TARGETS", False);
	incr = XInternAtom(display, "INCR", False);
	XSetSelectionOwner(display, clipboard, window, CurrentTime);
	XFlush(display);
	if (XGetSelectionOwner(display, clipboard) != window)
		return 1;
	while (milliseconds() < deadline) {
		if (wait_x(display, 100) <= 0)
			continue;
		while (XPending(display) != 0) {
			XNextEvent(display, &event);
			if (event.type == PropertyNotify && incr_requestor != None &&
			    event.xproperty.window == incr_requestor && event.xproperty.atom == incr_property &&
			    event.xproperty.state == PropertyDelete) {
				size_t left = strlen(text) - incr_offset;
				size_t chunk = left > 4096 ? 4096 : left;

				XChangeProperty(display, incr_requestor, incr_property, utf8, 8, PropModeReplace,
				    chunk == 0 ? NULL : (const unsigned char *)text + incr_offset, (int)chunk);
				incr_offset += chunk;
				if (left == 0) {
					incr_requestor = None;
					incr_property = None;
				}
				XFlush(display);
				continue;
			}
			if (event.type != SelectionRequest || event.xselectionrequest.selection != clipboard)
				continue;
			{
				XSelectionEvent reply;
				Atom list[3] = { targets, utf8, XA_STRING };

				memset(&reply, 0, sizeof(reply));
				reply.type = SelectionNotify;
				reply.display = event.xselectionrequest.display;
				reply.requestor = event.xselectionrequest.requestor;
				reply.selection = event.xselectionrequest.selection;
				reply.target = event.xselectionrequest.target;
				reply.time = event.xselectionrequest.time;
				reply.property = None;
				if (event.xselectionrequest.target == targets) {
					XChangeProperty(display, event.xselectionrequest.requestor,
					    event.xselectionrequest.property, XA_ATOM, 32, PropModeReplace,
					    (unsigned char *)list, 3);
					reply.property = event.xselectionrequest.property;
			} else if (incremental && event.xselectionrequest.target == utf8) {
					unsigned long total = (unsigned long)strlen(text);

					XSelectInput(display, event.xselectionrequest.requestor, PropertyChangeMask);
					XChangeProperty(display, event.xselectionrequest.requestor,
					    event.xselectionrequest.property, incr, 32, PropModeReplace,
					    (unsigned char *)&total, 1);
					incr_requestor = event.xselectionrequest.requestor;
					incr_property = event.xselectionrequest.property;
					incr_offset = 0;
					reply.property = event.xselectionrequest.property;
				} else if (event.xselectionrequest.target == utf8 ||
				    event.xselectionrequest.target == XA_STRING) {
					XChangeProperty(display, event.xselectionrequest.requestor,
					    event.xselectionrequest.property, event.xselectionrequest.target, 8,
					    PropModeReplace, (const unsigned char *)text, (int)strlen(text));
					reply.property = event.xselectionrequest.property;
				}
				XSendEvent(display, event.xselectionrequest.requestor, False, 0,
				    (XEvent *)&reply);
				XFlush(display);
			}
		}
	}
	XDestroyWindow(display, window);
	return 0;
}

static int
request(Display *display)
{
	Window window;
	Atom clipboard, utf8, property, type;
	XEvent event;
	int format;
	unsigned long nitems, after;
	unsigned char *data = NULL;
	long long deadline = milliseconds() + 3000;

	window = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, 1, 1, 0, 0, 0);
	clipboard = XInternAtom(display, "CLIPBOARD", False);
	utf8 = XInternAtom(display, "UTF8_STRING", False);
	property = XInternAtom(display, "SUPERCLIP_TEST_REQUEST", False);
	XConvertSelection(display, clipboard, utf8, property, window, CurrentTime);
	XFlush(display);
	while (milliseconds() < deadline) {
		if (wait_x(display, 100) <= 0)
			continue;
		while (XPending(display) != 0) {
			XNextEvent(display, &event);
			if (event.type != SelectionNotify)
				continue;
			if (event.xselection.property == None)
				return 1;
			if (XGetWindowProperty(display, window, property, 0, 65536, True, AnyPropertyType,
				&type, &format, &nitems, &after, &data) != Success || format != 8 || after != 0)
				return 1;
			if (data != NULL) {
				if (fwrite(data, 1, nitems, stdout) != nitems)
					return 1;
				XFree(data);
			}
			XDestroyWindow(display, window);
			return 0;
		}
	}
	XDestroyWindow(display, window);
	return 1;
}

int
main(int argc, char **argv)
{
	Display *display;
	int ret;

	if (argc < 2 || ((strcmp(argv[1], "--owner") == 0 ||
	    strcmp(argv[1], "--owner-incr") == 0) && argc != 3) ||
	    (strcmp(argv[1], "--request") == 0 && argc != 2))
		return 2;
	display = XOpenDisplay(NULL);
	if (display == NULL)
		return 1;
	if (strcmp(argv[1], "--owner") == 0)
		ret = owner(display, argv[2], 0);
	else if (strcmp(argv[1], "--owner-incr") == 0)
		ret = owner(display, argv[2], 1);
	else if (strcmp(argv[1], "--request") == 0)
		ret = request(display);
	else
		ret = 2;
	XCloseDisplay(display);
	return ret;
}
