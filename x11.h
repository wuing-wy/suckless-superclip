#ifndef SUPERCLIP_X11_H
#define SUPERCLIP_X11_H

#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>

#include <stddef.h>

struct sc_draw_item {
	const char *title;
	const char *description;
};

struct sc_x11 {
	Display *display;
	int screen;
	Window root;
	Window window;
	Visual *visual;
	Colormap colormap;
	XftDraw *draw;
	XftFont *font;
	XftFont *configured_fallback;
	XftFont *fallback_fonts[16];
	size_t nfallback_fonts;
	size_t fallback_scanned;
	FcFontSet *fallback_set;
	XftColor foreground;
	XftColor background;
	XftColor selected;
	XIM im;
	XIC ic;
	Atom utf8_string;
	Atom clipboard;
	Atom targets;
	Atom paste_property;
	int width;
	int height;
	int line_height;
	int padding;
	int visible_rows;
};

int sc_x11_open(struct sc_x11 *x11);
void sc_x11_close(struct sc_x11 *x11);
int sc_x11_fd(const struct sc_x11 *x11);
void sc_x11_draw(struct sc_x11 *x11, const char *prompt, const char *query, size_t cursor,
    const struct sc_draw_item *items, size_t nitems, size_t selected, const char *status);
int sc_x11_lookup(struct sc_x11 *x11, XKeyEvent *event, char **text, KeySym *keysym);
void sc_x11_request_paste(struct sc_x11 *x11);
int sc_x11_take_paste(struct sc_x11 *x11, XSelectionEvent *event, char **text, size_t *len);

#endif
