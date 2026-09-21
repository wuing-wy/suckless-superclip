#include "x11.h"

#include <stdio.h>
#include <string.h>

int
main(void)
{
	struct sc_x11 x11;

	if (sc_x11_open(&x11) < 0) {
		fputs("cannot open X11 frontend\n", stderr);
		return 1;
	}
	if (sc_x11_fd(&x11) < 0) {
		sc_x11_close(&x11);
		return 1;
	}
	sc_x11_draw(&x11, "> ", "unicode: 你好 👨‍👩‍👦", strlen("unicode: 你好 👨‍👩‍👦"), NULL, 0, 0, "");
	sc_x11_close(&x11);
	return 0;
}
