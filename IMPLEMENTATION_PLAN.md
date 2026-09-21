# Implementation plan

The project is in checkpoint 5. The main components work, but the implementation
does not yet meet every acceptance requirement in `SPEC.md`.

## Checkpoint record

| Checkpoint | State | Verification and remaining scope |
| --- | --- | --- |
| 0 — inspect and plan | Complete | `pkg-config --exists x11 xft fontconfig xinerama xfixes libgrapheme`; planning documents present |
| 1 — portable foundations | Reopened | `./tests/test_foundation`; UTF-8 and protocol boundary fixes remain |
| 2 — X11 frontend | Reopened | `tests/run_xvfb.sh`; XIM, placement, input, and paste gaps remain |
| 3 — extensions | Reopened | `./tests/test_foundation`; lifecycle, fairness, bounds, and GUI tests remain |
| 4 — clipboard | Reopened | `./tests/test_foundation` and `tests/run_xvfb.sh`; persistence, IPC, and boundary gaps remain |
| 5 — integration and hardening | Active | `make check`, `make test-sanitize`; both pass only the current incomplete suite |

## 1. Correctness fixes

- Move colors, key bindings, clipboard capacity limits, and fallback-font cache
  capacity into compile-time configuration.
- Compile the clipboard extension with core flags only; reserve XFixes flags and
  libraries for `superclip-clipboardd`.
- Require a usable XIM/XIC and retry `Xutf8LookupString` with a bounded dynamic
  buffer after `XBufferOverflow`.
- Reject every invalid UTF-8 form, including overlong three- and four-byte
  encodings, before it reaches queries, protocol fields, or history.
- Bound extension discovery before entries reach the fixed-size draw array, and
  reject overlong root/joined paths without using truncated values.
- Accept `STATUS` setup records; bound setup output and duration; require exactly
  one description record.
- Send `QUIT` before closing a persistent session, then retain the existing
  terminate/kill fallback.
- Fail a session after the item limit, drain through `END`, and ignore only
  valid stale responses. Reject future or otherwise wrong request IDs, reject
  invalid response kinds during execution, and do not display stale `ERROR`
  records.
- Allow every field whose encoded record fits the protocol limit; do not reject
  long unescaped ASCII fields based on their worst-case escaped size.
- Clear the execute phase after a persistent action error, and show an unknown
  direct subcommand as a GUI error as required by the specification.
- Limit stdout and stderr work per poll iteration so X11 input stays responsive.
  Reject trailing records from short operations, and show only the first stderr
  line after a generic process error.
- Center on the selected monitor's work area rather than its full geometry.
- Scope clipboard daemon socket and lock paths by `DISPLAY`; startup locking,
  termination cleanup, and stale-socket recovery are implemented and covered by
  Xvfb tests.
- Enforce the 8 MiB state-file limit by dropping oldest entries during
  compaction. Trim whitespace before applying the per-entry size limit.
- Convert `STRING` selection data from Latin-1 to UTF-8. Repair or rewrite a
  corrupt history tail before appending new records.
- Serve large owned selections with ICCCM `INCR` when they exceed the X server's
  single-request limit. Return `OK` only after allocation succeeds and the daemon
  confirms that it owns `CLIPBOARD`.

## 2. Acceptance tests

- Add protocol tests for complete escape round trips, NUL, invalid UTF-8,
  record/item limits, drain behavior, and interleaved stale/future request IDs.
- Cover all required grapheme cases, including skin-tone modifiers, and test
  extension precedence, invalid names, executable targets, and symlinks.
- Drive the real GUI under Xvfb through lazy selection, direct subcommands, all
  three query modes, debounce, edit-after-result, stale results, action
  success/failure, hung-child Escape, cleanup, keyboard editing, and paste.
- Verify that `superclip setup` reports every interface without changing files,
  starting services, or creating autostart entries.
- Extend clipboard tests with daemon absence, duplicate startup, separate
  displays, restart persistence, all size limits, corrupt state, and `STRING`
  fallback. Verify that an item near the 256 KiB limit remains fully available
  after the frontend and extension exit.

## 3. Final verification

- Run the complete build and sanitizer matrix.
- Repeat the adversarial source review after all fixes.
- Reconcile every row in `REQUIREMENTS.md`; completion requires every row to be
  **Verified**.

```sh
make
make test
make check
make test-sanitize
```
