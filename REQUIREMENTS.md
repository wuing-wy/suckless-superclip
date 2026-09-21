# Requirements traceability

`SPEC.md` is authoritative. **Verified** means the behavior is implemented and
has proportionate verification. **Implementation gap** and **Test gap** may
appear together when both the behavior and its required verification are
incomplete.

| SPEC | Requirement | Code and verification | Status |
| --- | --- | --- | --- |
| 1 | Transient X11 command panel; features live in extensions | `main.c:main/draw/select_extension`, `extension.c:sc_child_spawn`; lazy startup integration test absent | Test gap |
| 2.1 | Required v1 feature set | `main.c`, `x11.c`, `extension.c`, `protocol.c`, `clipboard/`; detailed rows below | Implementation gap; Test gap |
| 2.2 | Product scope remains keyboard-driven, text-only, X11-only | `main.c:handle_key/draw`, `x11.c:sc_x11_draw`, `Makefile`; manual scope review, no runtime test applicable | Verified |
| 3.1 | C99/POSIX and the specified core X11/libgrapheme dependencies | `config.mk:X11_CFLAGS/X11_LIBS`; `make check` on 2026-09-21 | Verified |
| 3.2 | Only the clipboard daemon additionally depends on XFixes | `Makefile:clipboard/%.o` uses `CLIP_CFLAGS` for the extension too | Implementation gap |
| 3.3 | POSIX make and compile-time configuration of all listed settings | `Makefile:config.h`, `config.def.h`; colors/keys in `x11.c`/`main.c` and limits in `store.h`/`x11.h` remain hard-coded | Implementation gap |
| 4 | Two extension roots, precedence, flat discovery, valid executable targets | `extension.c:discover_dir/sc_extensions_discover`, `main.c:draw`; list growth and truncated paths are unsafe; boundary tests absent | Implementation gap; Test gap |
| 5.1 | Selector lists extensions without starting them | `main.c:draw`, `main.c:select_extension`; no GUI integration test | Test gap |
| 5.2 | Direct subcommand selects an extension and joins the initial query | `main.c:main`; unknown names exit before GUI creation, and direct-mode tests are absent | Implementation gap; Test gap |
| 5.3 | Read-only setup handles bounded description, advice, and status checks | `main.c:read_description/run_setup`; `STATUS` and output bounds are wrong; read-only test absent | Implementation gap; Test gap |
| 6 | Core/session lifecycle, `QUIT`, cleanup, and independent instances | `main.c:close_child`, `extension.c:sc_child_close`; `QUIT` is never sent and UI cleanup test is absent | Implementation gap; Test gap |
| 7.1 | Override-redirect window, drawing, raising, and keyboard grab | `x11.c:sc_x11_open/sc_x11_draw`, `tests/test_x11.c:main`; failure paths lack Xvfb tests | Test gap |
| 7.2 | Focus/pointer monitor choice and work-area centering | `x11.c:window_center`; full geometry is used and placement test is absent | Implementation gap; Test gap |
| 7.3 | XIM UTF-8 input, grapheme editing, font fallback, and paste | `util.c:sc_utf8_valid`, `text.c`, `x11.c:sc_x11_lookup/sc_x11_take_paste`; implementation and tests incomplete | Implementation gap; Test gap |
| 7.4 | Required keyboard commands | `main.c:handle_key`; no X11 key-event test | Test gap |
| 8.1 | Explicit UI states and flat text results | `main.c:app_state/draw`; no state-machine integration test | Test gap |
| 8.2 | Three valid process/trigger combinations and 50 ms debounce | `extension.c:sc_extension_apply_description`, `main.c:after_description`; parser unit exists, timing test absent | Test gap |
| 8.3 | Short query/action flow and edit-after-result behavior | `main.c:queue_query/queue_execute/edited_query`; GUI lifecycle test absent | Test gap |
| 8.4 | Persistent requests, increasing IDs, and stale-response draining | `main.c:queue_query/handle_record`; stale `ERROR` is wrong and interleaving tests are absent | Implementation gap; Test gap |
| 8.5 | Extension-owned ordering/IDs and action success/error behavior | `main.c:add_result/handle_record`; persistent error recovery is wrong and GUI action tests are absent | Implementation gap; Test gap |
| 9.1 | UTF-8 line records, escaping, validation, and version rejection | `protocol.c:sc_protocol_parse`, `util.c:sc_utf8_valid`, `main.c:handle_record`; implementation and round-trip/NUL tests incomplete | Implementation gap; Test gap |
| 9.2 | 65,536-byte records, 256-item failure, and drain through `END` | `protocol.c:sc_protocol_escape`, `main.c:add_result/handle_record`; limits/drain wrong and tests absent | Implementation gap; Test gap |
| 9.3 | Exactly one valid description and rejection of `short + change` | `extension.c:sc_extension_apply_description`, `main.c:select_extension`; trailing output accepted and GUI test absent | Implementation gap; Test gap |
| 9.4 | `NONE`, `AUTOSTART`, and `STATUS` setup records | `protocol.c:sc_protocol_validate`, `main.c:run_setup`; `STATUS` rejected and record tests incomplete | Implementation gap; Test gap |
| 9.5 | Short query framing, item sequence, empty results, and errors | `main.c:queue_query/handle_record`, `extension.c:sc_child_*`; trailing records are accepted and lifecycle tests are absent | Implementation gap; Test gap |
| 9.6 | Short execute includes query/result ID and returns `OK` or `ERROR` | `main.c:queue_execute/handle_record`; invalid responses are accepted and execute tests are absent | Implementation gap; Test gap |
| 9.7 | Persistent interleaving, stale IDs, `QUIT`, and EOF exit | `main.c:handle_record/close_child`, `extension.c:sc_child_queue`; ID/error/`QUIT` behavior and tests incomplete | Implementation gap; Test gap |
| 9.8 | Direct execution, responsive nonblocking I/O, bounded stderr, and reaping | `extension.c:sc_child_spawn/sc_child_read_record/sc_child_drain_stderr/sc_child_close`, `main.c:handle_child`; fairness/error display and tests incomplete | Implementation gap; Test gap |
| 10.1 | Public clipboard extension, explicit daemon startup, and setup status | `clipboard/extension.c:main`, `main.c:run_setup`; status is missing and daemon absence is untested | Implementation gap; Test gap |
| 10.2 | Private socket and one daemon per user and `DISPLAY` | `clipboard/daemon.c:paths/open_lock/open_socket/cleanup_daemon`, `tests/run_xvfb.sh`; duplicate startup, stale recovery, and normal cleanup pass, but paths are not scoped by `DISPLAY` | Implementation gap |
| 10.3 | XFixes capture, startup capture, UTF8/STRING, and bounded `INCR` | `clipboard/daemon.c:read_selection/read_incr_chunk/handle_x`; `STRING` conversion and boundary tests absent | Implementation gap; Test gap |
| 10.4 | Trim, adjacent deduplication, entry/count/state limits, newest first | `clipboard/store.c:sc_clip_store_add/compact_store`; trim/limits wrong and tests incomplete | Implementation gap; Test gap |
| 10.5 | Secure plaintext persistence and atomic compaction | `clipboard/store.c`, `clipboard/daemon.c:paths`; corrupt tails, parent creation, and size cap are incomplete, as are recovery/restart tests | Implementation gap; Test gap |
| 10.6 | Selected item remains owned and served after UI exit | `clipboard/daemon.c:own_text/respond_execute/handle_selection_request`; failure/large transfer wrong and lifecycle test absent | Implementation gap; Test gap |
| 10.7 | `status` and `clear` do not start another daemon | `clipboard/daemon.c:control_command`, `tests/run_xvfb.sh`; running controls pass, absence is untested | Test gap |
| 11 | Explicit errors, bounded resources, responsive exit, and cleanup | `main.c:main/handle_child`, `extension.c:sc_child_*`, `clipboard/daemon.c:main`; GUI errors/bounds and matrix incomplete | Implementation gap; Test gap |
| 12 | Small C modules, explicit state, public process protocol | module boundaries in `main.c`, `x11.c`, `text.c`, `extension.c`, `protocol.c`, `util.c`, `clipboard/`; manual architecture review, no runtime test applicable | Verified |
| 13.1 | Required protocol, text, discovery, and store unit tests | `tests/test_foundation.c`; missing cases are listed below | Test gap |
| 13.2 | Required Xvfb integration matrix | `tests/run_xvfb.sh`; missing cases are listed below | Test gap |
| 13.3 | Every completion condition is demonstrated | This table; open implementation and test gaps remain | Implementation gap; Test gap |
| 14 | New core behavior requires a concrete need and protocol versioning | `SPEC.md` §14 and `AGENTS.md` Product invariants; document-policy review, no code test applicable | Verified |

## Missing acceptance tests

Unit coverage still needs protocol NUL and complete escape round trips, invalid
and overlong UTF-8, record/item limits with drain behavior, interleaved
stale/future IDs, skin-tone graphemes, user/system precedence, non-executable and
invalid symlink targets, storage size/compaction, and recovery after corruption.

Xvfb coverage still needs lazy extension startup, direct subcommands, all three
query modes, debounce, stale responses, edit-after-result, action success and
failure, Escape during a hung child, process cleanup, setup read-only behavior,
keyboard/XIM/paste paths, daemon absence and duplicate/display scoping, restart
persistence, clipboard bounds and `STRING` fallback, and near-limit selection
ownership.

## Verification record

On 2026-09-21, `make check` and `make test-sanitize` passed in the current
environment. Those commands validate the tests that exist; they do not close
the gaps listed above.
