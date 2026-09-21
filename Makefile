include config.mk

CORE_OBJS = main.o x11.o extension.o protocol.o text.o util.o
TEST_OBJS = tests/test_foundation.o protocol.o text.o util.o extension.o clipboard/store.o

.PHONY: all clean test check test-sanitize install uninstall

all: config.h superclip superclip-clipboardd clipboard-extension

config.h: config.def.h
	cp config.def.h config.h

%.o: %.c config.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(X11_CFLAGS) -c -o $@ $<

clipboard/%.o: clipboard/%.c config.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CLIP_CFLAGS) -c -o $@ $<

tests/%.o: tests/%.c config.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(X11_CFLAGS) -c -o $@ $<

superclip: $(CORE_OBJS)
	$(CC) $(LDFLAGS) -o $@ $(CORE_OBJS) $(X11_LIBS)

superclip-clipboardd: clipboard/daemon.o clipboard/store.o protocol.o util.o
	$(CC) $(LDFLAGS) -o $@ clipboard/daemon.o clipboard/store.o protocol.o util.o $(CLIP_LIBS)

clipboard-extension: clipboard/extension.o protocol.o util.o
	$(CC) $(LDFLAGS) -o $@ clipboard/extension.o protocol.o util.o

tests/fake_extension: tests/fake_extension.o
	$(CC) $(LDFLAGS) -o $@ tests/fake_extension.o

tests/test_foundation: $(TEST_OBJS) tests/fake_extension
	$(CC) $(LDFLAGS) -o $@ $(TEST_OBJS) -lgrapheme

tests/test_x11: tests/test_x11.o x11.o text.o util.o
	$(CC) $(LDFLAGS) -o $@ tests/test_x11.o x11.o text.o util.o $(X11_LIBS)

tests/clipboard_x11: tests/clipboard_x11.o
	$(CC) $(LDFLAGS) -o $@ tests/clipboard_x11.o $(CLIP_LIBS)

test: tests/test_foundation tests/test_x11 tests/clipboard_x11 superclip-clipboardd clipboard-extension
	./tests/test_foundation
	./tests/run_xvfb.sh

check: clean all test

test-sanitize:
	$(MAKE) clean
	ASAN_OPTIONS=detect_leaks=0; export ASAN_OPTIONS; \
	$(MAKE) CFLAGS='-std=c99 -D_XOPEN_SOURCE=700 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wpedantic -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' LDFLAGS='-fsanitize=address,undefined' test

install: all
	install -d $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(LIBEXECDIR)
	install -m 0755 superclip $(DESTDIR)$(PREFIX)/bin/superclip
	install -m 0755 superclip-clipboardd $(DESTDIR)$(PREFIX)/bin/superclip-clipboardd
	install -m 0755 clipboard-extension $(DESTDIR)$(LIBEXECDIR)/clipboard

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/superclip $(DESTDIR)$(PREFIX)/bin/superclip-clipboardd $(DESTDIR)$(LIBEXECDIR)/clipboard

clean:
	rm -f *.o clipboard/*.o tests/*.o superclip superclip-clipboardd clipboard-extension tests/fake_extension tests/test_foundation tests/test_x11 tests/clipboard_x11
