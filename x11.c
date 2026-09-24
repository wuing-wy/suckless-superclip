#include "config.h"
#include "x11.h"

#include <X11/Xatom.h>
#include <X11/extensions/Xinerama.h>
#include <fontconfig/fontconfig.h>

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int
screen_for_point(XineramaScreenInfo *screens, int nscreens, int x, int y)
{
	int i;

	for (i = 0; i < nscreens; i++) {
		if (x >= screens[i].x_org && x < screens[i].x_org + screens[i].width &&
		    y >= screens[i].y_org && y < screens[i].y_org + screens[i].height)
			return i;
	}
	return -1;
}

static void
window_center(struct sc_x11 *x11, int *x, int *y)
{
	XineramaScreenInfo *screens = NULL;
	XWindowAttributes attributes;
	Window focused, child;
	int revert, n = 0, selected = -1;
	int root_x, root_y, win_x, win_y;
	unsigned int mask;
	int width, height;

	width = DisplayWidth(x11->display, x11->screen);
	height = DisplayHeight(x11->display, x11->screen);
	if (XineramaIsActive(x11->display))
		screens = XineramaQueryScreens(x11->display, &n);
	if (screens != NULL && n > 0) {
		XGetInputFocus(x11->display, &focused, &revert);
		if (focused != None && focused != PointerRoot &&
		    XGetWindowAttributes(x11->display, focused, &attributes) &&
		    XTranslateCoordinates(x11->display, focused, x11->root, 0, 0, &root_x, &root_y, &child))
			selected = screen_for_point(screens, n, root_x + attributes.width / 2,
			    root_y + attributes.height / 2);
		if (selected < 0 && XQueryPointer(x11->display, x11->root, &child, &focused,
		    &root_x, &root_y, &win_x, &win_y, &mask))
			selected = screen_for_point(screens, n, root_x, root_y);
		if (selected >= 0) {
			width = screens[selected].width;
			height = screens[selected].height;
			*x = screens[selected].x_org + (width - x11->width) / 2;
			*y = screens[selected].y_org + (height - x11->height) / 2;
			XFree(screens);
			return;
		}
		XFree(screens);
	}
	*x = (width - x11->width) / 2;
	*y = (height - x11->height) / 2;
}

int
sc_x11_open(struct sc_x11 *x11)
{
	XSetWindowAttributes attributes;
	XIMStyles *styles = NULL;
	XIMStyle input_style = XIMPreeditNothing | XIMStatusNothing;
	FcPattern *pattern;
	FcResult result;
	int x, y, n;

	if (x11 == NULL)
		return -1;
	memset(x11, 0, sizeof(*x11));
	x11->display = XOpenDisplay(NULL);
	if (x11->display == NULL)
		return -1;
	x11->screen = DefaultScreen(x11->display);
	x11->root = RootWindow(x11->display, x11->screen);
	x11->visual = DefaultVisual(x11->display, x11->screen);
	x11->colormap = DefaultColormap(x11->display, x11->screen);
	x11->font = XftFontOpenName(x11->display, x11->screen, SUPERCLIP_FONT);
	if (x11->font == NULL)
		goto fail;
	x11->configured_fallback = XftFontOpenName(x11->display, x11->screen,
	    SUPERCLIP_FALLBACK_FONT);
	pattern = FcNameParse((const FcChar8 *)SUPERCLIP_FONT);
	if (pattern != NULL) {
		FcConfigSubstitute(NULL, pattern, FcMatchPattern);
		FcDefaultSubstitute(pattern);
		x11->fallback_set = FcFontSort(NULL, pattern, FcTrue, NULL, &result);
		FcPatternDestroy(pattern);
	}
	x11->padding = SUPERCLIP_PADDING;
	x11->visible_rows = SUPERCLIP_VISIBLE_ROWS;
	x11->line_height = x11->font->ascent + x11->font->descent + 4;
	x11->width = SUPERCLIP_WIDTH;
	x11->height = x11->padding * 2 + x11->line_height * (x11->visible_rows + 1);
	window_center(x11, &x, &y);
	attributes.override_redirect = True;
	attributes.background_pixel = BlackPixel(x11->display, x11->screen);
	attributes.event_mask = ExposureMask | KeyPressMask | StructureNotifyMask | PropertyChangeMask;
	x11->window = XCreateWindow(x11->display, x11->root, x, y, (unsigned int)x11->width,
	    (unsigned int)x11->height, 0, DefaultDepth(x11->display, x11->screen), InputOutput,
	    x11->visual, CWOverrideRedirect | CWBackPixel | CWEventMask, &attributes);
	if (x11->window == None)
		goto fail;
	x11->draw = XftDrawCreate(x11->display, x11->window, x11->visual, x11->colormap);
	if (x11->draw == NULL)
		goto fail;
	if (!XftColorAllocName(x11->display, x11->visual, x11->colormap, SUPERCLIP_FOREGROUND, &x11->foreground) ||
	    !XftColorAllocName(x11->display, x11->visual, x11->colormap, SUPERCLIP_PROMPT, &x11->prompt) ||
	    !XftColorAllocName(x11->display, x11->visual, x11->colormap, SUPERCLIP_BACKGROUND, &x11->background) ||
	    !XftColorAllocName(x11->display, x11->visual, x11->colormap, SUPERCLIP_SELECTED, &x11->selected))
		goto fail;
	x11->utf8_string = XInternAtom(x11->display, "UTF8_STRING", False);
	x11->clipboard = XInternAtom(x11->display, "CLIPBOARD", False);
	x11->targets = XInternAtom(x11->display, "TARGETS", False);
	x11->paste_property = XInternAtom(x11->display, "SUPERCLIP_PASTE", False);
	x11->im = XOpenIM(x11->display, NULL, NULL, NULL);
	if (x11->im != NULL) {
		styles = NULL;
		if (XGetIMValues(x11->im, XNQueryInputStyle, &styles, NULL) == NULL && styles != NULL) {
			for (n = 0; n < styles->count_styles; n++)
				if (styles->supported_styles[n] == input_style)
					break;
			if (n == styles->count_styles)
				input_style = styles->supported_styles[0];
			XFree(styles);
			x11->ic = XCreateIC(x11->im, XNInputStyle, input_style,
			    XNClientWindow, x11->window, XNFocusWindow, x11->window, NULL);
		}
	}
	XMapRaised(x11->display, x11->window);
	XSync(x11->display, False);
	if (XGrabKeyboard(x11->display, x11->window, True, GrabModeAsync, GrabModeAsync,
	    CurrentTime) != GrabSuccess)
		goto fail;
	if (x11->ic != NULL)
		XSetICFocus(x11->ic);
	return 0;
fail:
	sc_x11_close(x11);
	return -1;
}

void
sc_x11_close(struct sc_x11 *x11)
{
	if (x11 == NULL || x11->display == NULL)
		return;
	if (x11->ic != NULL) {
		XUnsetICFocus(x11->ic);
		XDestroyIC(x11->ic);
	}
	if (x11->im != NULL)
		XCloseIM(x11->im);
	XUngrabKeyboard(x11->display, CurrentTime);
	if (x11->draw != NULL)
		XftDrawDestroy(x11->draw);
	if (x11->font != NULL)
		XftFontClose(x11->display, x11->font);
	if (x11->configured_fallback != NULL)
		XftFontClose(x11->display, x11->configured_fallback);
	for (size_t i = 0; i < x11->nfallback_fonts; i++)
		XftFontClose(x11->display, x11->fallback_fonts[i]);
	if (x11->fallback_set != NULL)
		FcFontSetDestroy(x11->fallback_set);
	if (x11->window != None)
		XDestroyWindow(x11->display, x11->window);
	XCloseDisplay(x11->display);
	memset(x11, 0, sizeof(*x11));
}

int
sc_x11_fd(const struct sc_x11 *x11)
{
	return x11 == NULL || x11->display == NULL ? -1 : ConnectionNumber(x11->display);
}

static size_t
decode_utf8(const unsigned char *p, size_t available, FcChar32 *codepoint)
{
	if (p == NULL || codepoint == NULL || available == 0)
		return 0;
	if (p[0] < 0x80U) {
		*codepoint = p[0];
		return 1;
	}
	if (available >= 2 && (p[0] & 0xe0U) == 0xc0U && (p[1] & 0xc0U) == 0x80U) {
		*codepoint = ((FcChar32)(p[0] & 0x1fU) << 6) | (p[1] & 0x3fU);
		return 2;
	}
	if (available >= 3 && (p[0] & 0xf0U) == 0xe0U && (p[1] & 0xc0U) == 0x80U &&
	    (p[2] & 0xc0U) == 0x80U) {
		*codepoint = ((FcChar32)(p[0] & 0x0fU) << 12) |
		    ((FcChar32)(p[1] & 0x3fU) << 6) | (p[2] & 0x3fU);
		return 3;
	}
	if (available >= 4 && (p[0] & 0xf8U) == 0xf0U && (p[1] & 0xc0U) == 0x80U &&
	    (p[2] & 0xc0U) == 0x80U && (p[3] & 0xc0U) == 0x80U) {
		*codepoint = ((FcChar32)(p[0] & 0x07U) << 18) |
		    ((FcChar32)(p[1] & 0x3fU) << 12) |
		    ((FcChar32)(p[2] & 0x3fU) << 6) | (p[3] & 0x3fU);
		return 4;
	}
	return 0;
}

static XftFont *
font_for_codepoint(struct sc_x11 *x11, FcChar32 codepoint)
{
	XftFont *font = x11->font;

	if (XftCharIndex(x11->display, font, codepoint) == 0 &&
	    x11->configured_fallback != NULL &&
	    XftCharIndex(x11->display, x11->configured_fallback, codepoint) != 0)
		font = x11->configured_fallback;
	if (font == x11->font && XftCharIndex(x11->display, font, codepoint) == 0 &&
	    x11->fallback_set != NULL) {
		size_t i;
		for (i = 0; i < x11->nfallback_fonts; i++) {
			if (XftCharIndex(x11->display, x11->fallback_fonts[i], codepoint) != 0) {
				font = x11->fallback_fonts[i];
				break;
			}
		}
		if (font == x11->font) {
			for (i = x11->fallback_scanned; i < (size_t)x11->fallback_set->nfont &&
			    x11->nfallback_fonts < sizeof(x11->fallback_fonts) / sizeof(x11->fallback_fonts[0]); i++) {
				FcPattern *candidate_pattern = FcPatternDuplicate(x11->fallback_set->fonts[i]);
				XftFont *candidate;

				x11->fallback_scanned = i + 1;
				if (candidate_pattern == NULL)
					continue;
				candidate = XftFontOpenPattern(x11->display, candidate_pattern);
				if (candidate == NULL)
					continue;
				x11->fallback_fonts[x11->nfallback_fonts++] = candidate;
				if (XftCharIndex(x11->display, candidate, codepoint) != 0) {
					font = candidate;
					break;
				}
			}
		}
	}
	return font;
}

static void
draw_text(struct sc_x11 *x11, const XftColor *color, int x, int y, const char *text)
{
	const unsigned char *p = (const unsigned char *)text;
	FcChar32 codepoint;
	XftFont *font;
	XGlyphInfo info;
	size_t used, left;
	char bytes[5];

	if (text == NULL)
		return;
	left = strlen(text);
	while (left != 0) {
		used = decode_utf8(p, left, &codepoint);
		if (used == 0)
			return;
		font = font_for_codepoint(x11, codepoint);
		memcpy(bytes, p, used);
		bytes[used] = '\0';
		XftDrawStringUtf8(x11->draw, color, font, x, y, (const FcChar8 *)bytes, (int)used);
		XftTextExtentsUtf8(x11->display, font, (const FcChar8 *)bytes, (int)used, &info);
		x += (int)info.xOff;
		p += used;
		left -= used;
	}
}

static int
text_width_n(struct sc_x11 *x11, const char *text, size_t len)
{
	const unsigned char *p = (const unsigned char *)text;
	XGlyphInfo info;
	FcChar32 codepoint;
	XftFont *font;
	size_t used;
	int width = 0;

	while (len != 0) {
		used = decode_utf8(p, len, &codepoint);
		if (used == 0)
			break;
		font = font_for_codepoint(x11, codepoint);
		XftTextExtentsUtf8(x11->display, font, (const FcChar8 *)p, (int)used, &info);
		width += (int)info.xOff;
		p += used;
		len -= used;
	}
	return width;
}

static int
text_width(struct sc_x11 *x11, const char *text)
{
	return text_width_n(x11, text, strlen(text));
}

void
sc_x11_draw(struct sc_x11 *x11, const char *prompt, const char *query, size_t cursor,
    const struct sc_draw_item *items, size_t nitems, size_t selected, const char *status)
{
	size_t i, first = 0, shown;
	int y;

	if (x11 == NULL || x11->display == NULL)
		return;
	XftDrawRect(x11->draw, &x11->background, 0, 0, (unsigned int)x11->width, (unsigned int)x11->height);
	y = x11->padding + x11->font->ascent;
	if (cursor > strlen(query))
		cursor = strlen(query);
	draw_text(x11, &x11->prompt, x11->padding, y, prompt);
	draw_text(x11, &x11->foreground, x11->padding + text_width(x11, prompt) + 8, y, query);
	XftDrawRect(x11->draw, &x11->foreground,
	    x11->padding + text_width(x11, prompt) + 8 + text_width_n(x11, query, cursor),
	    y - x11->font->ascent, 1, (unsigned int)(x11->font->ascent + x11->font->descent));
	if (status != NULL && status[0] != '\0')
		draw_text(x11, &x11->foreground, x11->padding, y + x11->line_height, status);
	if (selected >= (size_t)x11->visible_rows)
		first = selected - (size_t)x11->visible_rows + 1;
	shown = nitems - first;
	if (shown > (size_t)x11->visible_rows)
		shown = (size_t)x11->visible_rows;
	for (i = 0; i < shown; i++) {
		size_t item = first + i;
		y = x11->padding + (int)(i + 2) * x11->line_height;
		if (item == selected)
			XftDrawRect(x11->draw, &x11->selected, 0, y - x11->font->ascent - 2,
			    (unsigned int)x11->width, (unsigned int)x11->line_height);
		draw_text(x11, &x11->foreground, x11->padding, y, items[item].title);
		if (items[item].description != NULL && items[item].description[0] != '\0')
			draw_text(x11, &x11->foreground, x11->padding + x11->width / 2, y,
			    items[item].description);
	}
	XFlush(x11->display);
}

int
sc_x11_lookup(struct sc_x11 *x11, XKeyEvent *event, char **text, KeySym *keysym)
{
	Status status;
	char stack[256];
	char *buf = stack;
	int n;

	if (x11 == NULL || event == NULL || text == NULL || keysym == NULL)
		return -1;
	*text = NULL;
	if (x11->ic != NULL)
		n = Xutf8LookupString(x11->ic, event, buf, (int)sizeof(stack) - 1, keysym, &status);
	else {
		n = XLookupString(event, buf, (int)sizeof(stack) - 1, keysym, NULL);
		status = XLookupBoth;
	}
	if (status == XBufferOverflow || n < 0)
		return -1;
	if (n == 0)
		return 0;
	buf[n] = '\0';
	*text = malloc((size_t)n + 1);
	if (*text == NULL)
		return -1;
	memcpy(*text, buf, (size_t)n + 1);
	return n;
}

void
sc_x11_request_paste(struct sc_x11 *x11)
{
	if (x11 != NULL && x11->display != NULL)
		XConvertSelection(x11->display, x11->clipboard, x11->utf8_string,
		    x11->paste_property, x11->window, CurrentTime);
}

int
sc_x11_take_paste(struct sc_x11 *x11, XSelectionEvent *event, char **text, size_t *len)
{
	Atom type;
	int format;
	unsigned long nitems, after;
	unsigned char *data = NULL;

	if (x11 == NULL || event == NULL || text == NULL || len == NULL ||
	    event->property != x11->paste_property)
		return -1;
	*text = NULL;
	*len = 0;
	if (XGetWindowProperty(x11->display, x11->window, x11->paste_property, 0,
	    SUPERCLIP_MAX_QUERY / 4, True, AnyPropertyType, &type, &format, &nitems,
	    &after, &data) != Success || data == NULL || type != x11->utf8_string ||
	    format != 8 || after != 0) {
		if (data != NULL)
			XFree(data);
		return -1;
	}
	*text = malloc(nitems + 1);
	if (*text == NULL) {
		XFree(data);
		return -1;
	}
	memcpy(*text, data, nitems);
	(*text)[nitems] = '\0';
	*len = nitems;
	XFree(data);
	return 0;
}
