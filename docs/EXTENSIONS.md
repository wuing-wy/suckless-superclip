# Writing extensions

[中文](EXTENSIONS_CN.md)

An extension is an executable file that exchanges protocol v1 records with
superclip over stdin and stdout. It may be written in any language.

## Install the executable

Put the file in one of these locations:

```text
$XDG_CONFIG_HOME/superclip/extensions/
$HOME/.config/superclip/extensions/    (when XDG_CONFIG_HOME is unset)
/usr/local/libexec/superclip/extensions/    (default system directory)
```

For a custom build, the system directory is under the prefix compiled into
superclip; keep `config.mk` and `config.h` consistent as described in the
[README](../README.md).

The file name is the extension name and CLI subcommand. Names begin with an
ASCII letter or digit and may also contain `.`, `_`, and `-`. The name `setup`
is reserved. Make the file executable.

A user extension takes precedence over a system extension with the same name.

## Minimal extension

Create the user extension directory first:

```sh
mkdir -p "${XDG_CONFIG_HOME:-$HOME/.config}/superclip/extensions"
```

Save this example as `hello` in that directory. After saving it, make it
executable:

```sh
chmod 755 "${XDG_CONFIG_HOME:-$HOME/.config}/superclip/extensions/hello"
```

```python
#!/usr/bin/env python3
import sys

mode = sys.argv[1] if len(sys.argv) == 2 else ""

if mode == "--superclip-describe":
    print("SUPERCLIP\t1\thello\tshort\tenter\tHello")
elif mode == "--superclip-setup":
    print("NONE")
elif mode == "--superclip-query":
    kind, request_id, query = sys.stdin.readline().rstrip("\n").split("\t", 2)
    print(f"BEGIN\t{request_id}")
    print(f"ITEM\t{request_id}\thello\tSay hello\tA minimal result")
    print(f"END\t{request_id}")
elif mode == "--superclip-execute":
    kind, request_id, query, result_id = \
        sys.stdin.readline().rstrip("\n").split("\t", 3)
    if result_id == "hello":
        print(f"OK\t{request_id}")
    else:
        print(f"ERROR\t{request_id}\tUnknown result")
else:
    raise SystemExit(2)
```

Check its description, then open it:

```sh
superclip setup
superclip hello
```

The example returns one fixed result. A real extension should validate every
record, decode escaped fields, enforce the protocol limits, and write
diagnostics to stderr.

## Choose a lifecycle

| Process mode | Trigger | Use |
| --- | --- | --- |
| `short` | `enter` | Start a process for each query and action. |
| `persistent` | `enter` | Reuse one process while the panel is open; query on `Enter`. |
| `persistent` | `change` | Reuse one process and query after input settles. |

A persistent extension implements `--superclip-session` and exits on `QUIT` or
EOF. It can receive another request before finishing the previous one, so every
response must carry the request ID it belongs to.

## Return setup advice

Protocol v1 allows `NONE`, `AUTOSTART`, and `STATUS` from
`--superclip-setup`. Use them for no setup, a suggested startup command, and a
current status respectively. The current frontend accepts `NONE` and
`AUTOSTART` but still rejects `STATUS`; that gap is tracked in
[PROGRESS.md](../PROGRESS.md). Setup records are informational, and the user
decides whether to act on them.

See [Extension protocol v1](PROTOCOL.md) for record formats, escaping, limits,
and error handling. The complete normative definition is in
[SPEC.md](../SPEC.md#9-扩展协议-v1).
