PREFIX = /usr/local
LIBEXECDIR = $(PREFIX)/libexec/superclip/extensions
CC = cc
PKG_CONFIG = pkg-config
PKG_CONFIG_PATH ?= /usr/local/lib/pkgconfig:/usr/lib/pkgconfig:/usr/share/pkgconfig
PKG_CONFIG_ENV = PKG_CONFIG_PATH='$(PKG_CONFIG_PATH)'
CFLAGS = -std=c99 -D_XOPEN_SOURCE=700 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wpedantic -O2
CPPFLAGS = -I.
LDFLAGS =
X11_CFLAGS = $(shell $(PKG_CONFIG_ENV) $(PKG_CONFIG) --cflags x11 xft fontconfig xinerama libgrapheme)
X11_LIBS = $(shell $(PKG_CONFIG_ENV) $(PKG_CONFIG) --libs x11 xft fontconfig xinerama libgrapheme)
CLIP_CFLAGS = $(shell $(PKG_CONFIG_ENV) $(PKG_CONFIG) --cflags x11 xfixes)
CLIP_LIBS = $(shell $(PKG_CONFIG_ENV) $(PKG_CONFIG) --libs x11 xfixes)
