# Using superclip

[中文](USAGE_CN.md)

## Open the panel

Open the extension list:

```sh
superclip
```

Select an extension with `Up` and `Down`, then press `Enter`. To open an
extension directly, give its exact name:

```sh
superclip clipboard
superclip clipboard meeting notes
```

Additional arguments are joined with spaces and placed in the input box as the
initial query.

## Keys

| Key | Action |
| --- | --- |
| `Enter` | Choose an extension, submit a query, or run the selected result |
| `Up` / `Down` | Select the previous or next item |
| `Ctrl-p` / `Ctrl-n` | Select the previous or next item |
| `Left` / `Right` | Move by one Unicode grapheme cluster |
| `Home` / `End` | Move to the beginning or end of the query |
| `Backspace` / `Delete` | Delete one Unicode grapheme cluster |
| `Ctrl-v` | Paste text from the X11 `CLIPBOARD` selection |
| `Escape` | Close the panel |

## Clipboard history

The `clipboard` extension needs `superclip-clipboardd`. Start it once per X11
session:

```sh
superclip-clipboardd &
```

Copied text then appears in `superclip clipboard`, newest first. Typing filters
the list. Select an item and press `Enter` to put it back on the clipboard.
Restoring entries near the 256 KiB limit is not yet reliable; this is tracked in
[PROGRESS.md](PROGRESS.md).

For entries within the raw 256 KiB limit, the daemon trims leading and trailing
Unicode whitespace and ignores empty results. A raw entry over that limit is
currently discarded before trimming. Capture is best effort: a clipboard owner
that exits or changes too quickly may be missed.

History is plaintext and may contain passwords, tokens, or other private text.
It is stored at `$XDG_STATE_HOME/superclip/clipboard`, or
`~/.local/state/superclip/clipboard` when `XDG_STATE_HOME` is unset. Use
`superclip-clipboardd clear` to remove it.

Useful commands:

```sh
superclip setup
superclip-clipboardd status
superclip-clipboardd clear
```

`superclip setup` prints extension setup advice. It does not apply the advice.

## Hotkeys

For dwm, add bindings like these to its `config.h`:

```c
{ MODKEY,           XK_space, spawn, SHCMD("superclip") },
{ MODKEY|ShiftMask, XK_v,     spawn, SHCMD("superclip clipboard") },
```

For sxhkd, add this to `sxhkdrc`:

```text
super + space
    superclip

super + shift + v
    superclip clipboard
```

## Configuration

`make` creates `config.h` from `config.def.h` when `config.h` does not exist.
Edit `config.h` to change fonts, panel dimensions, visible rows, debounce time,
and the protocol, query, or stderr limits, then rebuild and reinstall:

```sh
make clean
make
sudo make install
```

When changing the installation prefix, keep `PREFIX` in `config.mk` and
`SUPERCLIP_PREFIX` in `config.h` in sync.

Some limits, including clipboard history capacity and the fallback-font cache,
are not configurable there yet; see [PROGRESS.md](PROGRESS.md).

## Troubleshooting

- If the panel cannot open, check that the command runs inside an X11 session
  and that `DISPLAY` is set.
- If clipboard history is unavailable, start `superclip-clipboardd` in the same
  X11 session and run `superclip-clipboardd status`.
- If an extension is missing, check its directory, file name, and executable
  bit. Run `superclip setup` to check its description.

See [Writing extensions](docs/EXTENSIONS.md) to add another command.
