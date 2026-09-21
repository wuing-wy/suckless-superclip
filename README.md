# superclip

superclip is a keyboard-driven command panel for Linux/X11. Open it, choose an
extension, type a query, and run one of the returned actions. The included
`clipboard` extension provides searchable clipboard history.

The project is usable for development, but it does not yet satisfy every
acceptance requirement in `SPEC.md`. See [current progress](PROGRESS.md).

[中文](README_CN.md)

## Build

Required tools and libraries:

- a C99 compiler, POSIX `make`, and `pkg-config`
- Xlib, Xft, fontconfig, and Xinerama
- [libgrapheme](https://libs.suckless.org/libgrapheme/)
- XFixes for `superclip-clipboardd`
- Xvfb for integration tests

Build and test from the repository root:

```sh
make
make test
```

Install under `/usr/local`:

```sh
sudo make install
```

For another prefix, first run `make config.h`, then change both `PREFIX` in
`config.mk` and `SUPERCLIP_PREFIX` in `config.h`. The two values must match so
the installed binary can find system extensions.

## Start

Run `superclip` to choose an extension, or open one directly:

```sh
superclip
superclip clipboard
```

Bind either command in dwm, sxhkd, or your usual X11 hotkey tool. See the
[usage guide](USAGE.md) for keys, configuration, and example bindings.

## Clipboard history

Start the clipboard daemon once in your X11 session:

```sh
superclip-clipboardd &
```

You can add that command to `.xinitrc` or your window manager's autostart file.
Check its state or clear the history with:

```sh
superclip-clipboardd status
superclip-clipboardd clear
```

History is stored as plaintext at
`$XDG_STATE_HOME/superclip/clipboard`, or
`~/.local/state/superclip/clipboard` when `XDG_STATE_HOME` is unset. Clipboard
data often contains passwords, tokens, and other private text; protect or clear
this file accordingly.

## Documentation

- [Usage](USAGE.md)
- [Writing extensions](docs/EXTENSIONS.md)
- [Extension protocol v1](docs/PROTOCOL.md)
- [Design philosophy](docs/PHILOSOPHY.md)
- [Product specification (Chinese)](SPEC.md)

The English and Chinese guides link to each other.
