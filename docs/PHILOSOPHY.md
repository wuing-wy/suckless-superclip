# Design philosophy

[中文](PHILOSOPHY_CN.md)

## A focused frontend

superclip handles the interaction shared by every command: opening a panel,
editing a query, displaying text results, and running the selected action. It
starts when needed and exits when the interaction ends.

## Independent extensions

Each feature is an executable that speaks a small text protocol. Extensions
choose how to search, rank, and act on results, and they can be written in any
language. This keeps features independent of the frontend and makes them easy
to replace.

In the normal GUI flow, an extension starts only after the user selects it.
`superclip setup` is the explicit check that runs every extension's description
and setup interfaces. If a feature needs to keep state between panel
invocations, it owns that service and the user decides when to start it.
Clipboard history follows this model.

## Predictable behavior

The interface is a text box and a flat result list, with keyboard navigation
throughout. Configuration lives in `config.h` and is applied at build time.

The design requires fixed limits for protocol records, results, queries, and
persistent history. It also requires valid UTF-8 and grapheme-cluster cursor
movement. Known gaps between those requirements and the current implementation
are tracked in [PROGRESS.md](../PROGRESS.md).

## User-owned session setup

superclip works with the user's existing X11 session. The user chooses the
hotkey tool and decides whether an extension service belongs in session
startup. `superclip setup` lists each extension's recommended commands for the
user to review and apply.
