# superclip agent instructions

## Mission

Implement and maintain `superclip` as the small, auditable X11 command-panel framework defined by `SPEC.md`.

The repository must converge on a complete working implementation, not a mock-up or an architectural skeleton. Code, tests, installation behavior, and documentation are all part of the deliverable.

## Sources of truth

1. `SPEC.md` is authoritative for product behavior, public interfaces, protocol v1, dependencies, limits, and acceptance criteria.
2. This file is authoritative for engineering workflow and quality expectations.
3. Tests document verified behavior but do not override `SPEC.md`.

Read all of `SPEC.md` before planning or editing. Do not silently reinterpret it.

Do not edit `SPEC.md` unless the user explicitly requests a specification change. If the specification contains a genuine contradiction or an impossible requirement:

- record it in `SPEC_ISSUES.md` with the exact conflicting sections;
- continue all work that is not blocked by that issue;
- choose the smallest reversible implementation for internal details that the specification intentionally leaves open;
- ask the user only when a decision would change public behavior, protocol compatibility, security, or dependencies.

## Product invariants

These constraints are non-negotiable:

- Linux and X11 only; do not add Wayland support.
- The core is a transient frontend, never a daemon.
- Business features belong in extensions, not the core.
- Unselected extensions must not run during the normal GUI flow.
- Use C99 and a simple POSIX `make` build.
- The core dependencies are limited to libc/POSIX, Xlib, Xft, fontconfig, Xinerama, and libgrapheme.
- Only the clipboard daemon may additionally depend on XFixes.
- Do not introduce GTK, Qt, Cairo, JSON libraries, scripting runtimes, DBus, SQLite, systemd integration, or generic plugin/UI frameworks.
- Do not use `system()`, `popen()`, `/bin/sh -c`, or `dlopen()`.
- Never interpret queries, result IDs, setup text, or extension output as shell commands.
- Extension I/O is bounded, non-blocking, and integrated with the X11 fd through a single-threaded `poll()` loop.
- All external input, dynamic buffers, records, result sets, stderr capture, IPC messages, and persistent state must have explicit bounds.
- UTF-8 validity and complete Unicode grapheme-cluster editing are required behavior, not optional polish.
- UI results are text-only. Do not add icons, images, rich text, mouse support, previews, or nested views.
- Runtime configuration parsers are out of scope. User configuration is compile-time `config.h`.
- `superclip`, extensions, and the clipboard daemon must not secretly create or enable autostart entries.

When a convenient implementation conflicts with these invariants, preserve the invariant.

## Engineering style

Prefer code that a maintainer can audit directly:

- Use explicit state enums, ownership rules, sizes, and cleanup paths.
- Prefer small C modules with narrow responsibilities over reusable frameworks.
- Add an abstraction only after at least two real call sites require it.
- Avoid hidden control flow, macro metaprogramming, global mutable state without a clear owner, and speculative extension points.
- Keep functions focused, but do not fragment straightforward logic into one-line wrappers.
- Use tabs for indentation and a consistent K&R-style C layout.
- Check integer conversions, allocation arithmetic, short reads/writes, `EINTR`, `EAGAIN`, EOF, fork/exec failure, and partial initialization.
- Every opened fd, X resource, font, allocation, child process, socket, and temporary file needs an explicit release/reap path.
- Do not log through protocol stdout. Protocol stdout is data; diagnostics go to stderr.
- Comments should explain invariants, protocol reasoning, or non-obvious X11 behavior, not restate the code.
- Keep generated files, build output, temporary sockets, test state, and local `config.h` out of version control where appropriate.

Do not vendor a dependency merely because it is missing from the current machine. Detect missing development packages clearly and report the exact package or `pkg-config` module needed.

## Commit messages

Write commit messages in English using this form:

```text
type(scope): imperative summary
```

The scope is optional. Use the smallest type that describes the change:

- `feat`: add or extend user-visible behavior;
- `fix`: correct faulty behavior;
- `docs`: change documentation only;
- `test`: add or change tests only;
- `build`: change the build, installation, or dependencies;
- `refactor`: reorganize code without changing behavior;
- `chore`: make a maintenance change that fits none of the above.

Keep the summary lowercase, imperative, and at most 72 characters. A commit must
have one clear purpose. Add a body only when the reason or a non-obvious tradeoff
needs explanation; wrap it at 72 characters. Mark an incompatible public change
with a `BREAKING CHANGE:` footer.

Examples:

```text
feat(clipboard): add persistent history
fix(protocol): reject unknown escape sequences
docs: explain extension setup
```

## Required repository artifacts

The completed repository must contain at least:

- the core `superclip` sources;
- protocol, text-editing, X11, extension, and utility modules with clear boundaries;
- `config.def.h`, local-config handling, `config.mk`, and a POSIX `Makefile`;
- the system clipboard extension;
- `superclip-clipboardd`;
- unit tests and Xvfb-based integration tests;
- fake extensions used to test short, persistent, malformed, slow, and failing behavior;
- installation and usage documentation;
- `IMPLEMENTATION_PLAN.md` with checkpoints and verification commands;
- `REQUIREMENTS.md` mapping every normative `SPEC.md` section to code and tests;
- `PROGRESS.md` containing a short current checkpoint, completed verification, remaining work, and known blockers.

Keep planning and progress documents concise. They support implementation; they are not substitutes for it.

## Execution workflow

Work autonomously in the following checkpoints. A checkpoint is complete only after its relevant tests pass. Update `PROGRESS.md` after each checkpoint and continue without waiting for confirmation.

### Checkpoint 0: inspect and plan

- Inspect the complete worktree and dependency availability.
- Read `SPEC.md` completely.
- Create or update `IMPLEMENTATION_PLAN.md` and `REQUIREMENTS.md`.
- Define the intended source layout, public internal APIs, test strategy, and exact verification commands.
- Identify specification issues before they become code divergence.

Do not spend the entire run planning. Move into implementation once the plan is sufficient to guide the next checkpoint.

### Checkpoint 1: portable foundations

- Establish the Makefile/configuration skeleton.
- Implement bounded protocol v1 encoding, decoding, validation, and request/result structures.
- Implement the UTF-8 input buffer and libgrapheme-based cursor movement and deletion.
- Add focused unit tests for protocol limits, invalid data, escaping, overflow, and grapheme clusters.

This checkpoint must be testable without an X server.

### Checkpoint 2: X11 frontend

- Implement the override-redirect window, Xft drawing, font fallback, XIM input, Xinerama placement, keyboard grab, keyboard navigation, and clipboard paste.
- Preserve a responsive event loop and deterministic cleanup.
- Test separable state and text logic without X11; use Xvfb for behavior requiring a server.

Do not add extension business logic to the frontend.

### Checkpoint 3: extensions and process control

- Implement both extension directories, precedence, non-recursive discovery, symlink validation, reserved names, and lazy loading.
- Implement describe, setup, short query, short execute, persistent session, trigger modes, debounce, request IDs, stale-response draining, bounded stderr, child cleanup, and all protocol errors.
- Use `fork()` plus direct `execve()` of the resolved executable path. Never involve a shell.
- Add fake-extension integration tests covering normal, malformed, oversized, stale, slow, hung, early-exit, and action-error cases.

### Checkpoint 4: clipboard extension and daemon

- Implement the clipboard extension through the same public protocol as third-party extensions.
- Implement the foreground daemon, XFixes notifications, ICCCM selection transfer including `INCR`, bounded history, private Unix socket, single-instance behavior, persistence, compaction, status, and clear.
- Treat clipboard capture as best effort exactly as specified.
- Ensure the daemon can own and serve the selected history item after the frontend and extension exit.
- Add unit and Xvfb integration tests for capture, deduplication, bounds, persistence, selection ownership, status, clear, and daemon absence.

### Checkpoint 5: integration, review, and hardening

- Complete install/uninstall rules and user documentation.
- Verify `superclip setup` is read-only and reports clipboard autostart instructions without applying them.
- Run the complete test matrix.
- Perform a separate adversarial review of the full diff for protocol desynchronization, command injection, path races, integer overflow, use-after-free, fd leaks, zombies, signal races, stale request handling, X11 ownership errors, corrupt state recovery, and unbounded memory growth.
- Fix every confirmed P0/P1 issue and every in-scope test gap. Re-run the full test matrix after fixes.
- Remove unused abstractions, dead code, duplicate helpers, accidental dependencies, and features not required by `SPEC.md`.
- Reconcile `REQUIREMENTS.md` against the final implementation so every normative requirement is implemented and tested or explicitly identified as blocked.

## Build and verification contract

The build must expose these stable commands:

```sh
make
make test
make check
```

- `make` builds all production binaries and extensions.
- `make test` runs all unit tests that do not require a live desktop session and any automatically managed Xvfb integration tests.
- `make check` performs a clean build with strict warnings and then runs the full available test suite.

Also provide sanitizer execution, preferably as:

```sh
make test-sanitize
```

Use at least `-std=c99 -Wall -Wextra -Wpedantic` for project code. Sanitizers are a development verification tool, not a production dependency. If a tool such as Xvfb or a sanitizer is unavailable, do not fake success or weaken the tests; record the exact unavailable tool and run every remaining check.

Tests must be deterministic, must not use the user's real clipboard history, and must place sockets, state, and temporary files in private test directories. Tests must clean up their child processes and temporary state even after failure.

## Review rules

Review the implementation against the specification, not merely against whether it compiles.

Classify findings as:

- **P0:** command execution vulnerability, unsafe state corruption, uncontrolled deletion, or a core behavior that cannot terminate.
- **P1:** violation of `SPEC.md`, protocol incompatibility, unbounded resource use, fd/memory/process leak, race causing incorrect behavior, or missing required functionality.
- **P2:** maintainability problem, unnecessary complexity, weak diagnostic, or meaningful test gap.

For each finding, identify the file, line or symbol, triggering condition, and violated specification section. Confirm a finding before changing code. Fix all confirmed P0 and P1 findings before completion. Fix P2 findings when doing so stays within scope and improves the specified product.

After fixes, review the changed paths again; do not assume the first fix is correct.

## Scope and change control

- Do not modify files outside this repository.
- Do not install system packages or change the user's desktop/session configuration without explicit approval.
- Do not initialize Git, create commits, rewrite history, or publish anything unless explicitly requested.
- Do not broaden the project to support other platforms or protocols.
- Do not replace a difficult required feature with a stub, TODO, disabled test, or undocumented limitation.
- Do not mark a requirement complete solely because code exists; it needs proportionate verification.
- Preserve unrelated user changes and inspect the current worktree before editing overlapping files.

If an external prerequisite requires approval, request it with the exact command and purpose, then continue all independent work while waiting when possible.

## Definition of done

The project is complete only when all of the following are true:

1. Every required behavior in `SPEC.md` has an implementation.
2. `REQUIREMENTS.md` maps each normative specification section to concrete code and passing tests.
3. `make`, `make test`, and `make check` pass in the supported environment.
4. Sanitizer tests pass where the toolchain supports them.
5. Required X11 behavior is exercised under Xvfb, including extension failure and clipboard ownership paths.
6. The final adversarial review has no unresolved P0 or P1 findings.
7. No test is skipped merely to obtain a green run, and no required behavior is represented by a stub or TODO.
8. Closing the UI leaves no core or session-extension process, fd, socket, or temporary test state behind.
9. The core remains transient, single-threaded, text-only, shell-free, and free of unspecified production dependencies.
10. Installation and usage documentation are sufficient to build, install, configure a launcher key, run `superclip setup`, explicitly start the clipboard daemon, and write a minimal third-party extension.

Do not claim completion while any item above is unverified. If completion is genuinely blocked by unavailable hardware, display server capability, dependency, permission, or an unresolved specification decision, report the exact blocker, the evidence, completed work, and the smallest action needed from the user.
