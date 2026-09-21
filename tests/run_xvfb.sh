#!/bin/sh
set -eu

if ! command -v Xvfb >/dev/null 2>&1; then
	echo "Xvfb is required for integration tests" >&2
	exit 1
fi

test_root=$(mktemp -d "${TMPDIR:-/tmp}/superclip-xvfb.XXXXXX")
cleanup() {
	if [ "${owner_pid:-}" ]; then
		kill "$owner_pid" 2>/dev/null || true
		wait "$owner_pid" 2>/dev/null || true
	fi
	if [ "${daemon_pid:-}" ]; then
		kill "$daemon_pid" 2>/dev/null || true
		wait "$daemon_pid" 2>/dev/null || true
	fi
	if [ "${xvfb_pid:-}" ]; then
		if [ "${xvfb_sudo:-0}" -eq 1 ]; then
			sudo -n kill "$xvfb_pid" 2>/dev/null || true
		else
			kill "$xvfb_pid" 2>/dev/null || true
		fi
		wait "$xvfb_pid" 2>/dev/null || true
	fi
	rm -rf "$test_root"
}
trap cleanup EXIT HUP INT TERM

wait_for_xvfb() {
	for _ in 1 2 3 4 5 6 7 8 9 10; do
		if DISPLAY=:98 ./tests/test_x11 >/dev/null 2>&1; then
			return 0
		fi
		sleep 1
	done
	return 1
}

Xvfb :98 -screen 0 1024x768x24 >"$test_root/xvfb.log" 2>&1 &
xvfb_pid=$!
if ! wait_for_xvfb; then
	kill "$xvfb_pid" 2>/dev/null || true
	wait "$xvfb_pid" 2>/dev/null || true
	sudo -n Xvfb :98 -screen 0 1024x768x24 >"$test_root/xvfb.log" 2>&1 &
	xvfb_pid=$!
	xvfb_sudo=1
	if ! wait_for_xvfb; then
		cat "$test_root/xvfb.log" >&2
		exit 1
	fi
fi

DISPLAY=:98 ./tests/test_x11
mkdir -p "$test_root/runtime" "$test_root/state"
chmod 700 "$test_root/runtime" "$test_root/state"
XDG_RUNTIME_DIR="$test_root/runtime" XDG_STATE_HOME="$test_root/state" DISPLAY=:98 \
	./superclip-clipboardd >"$test_root/daemon.log" 2>&1 &
daemon_pid=$!
for _ in 1 2 3 4 5 6 7 8 9 10; do
	if XDG_RUNTIME_DIR="$test_root/runtime" DISPLAY=:98 ./superclip-clipboardd status >"$test_root/status" 2>/dev/null; then
		break
	fi
	sleep 1
done
grep '^ok	0	' "$test_root/status"

set +e
XDG_RUNTIME_DIR="$test_root/runtime" XDG_STATE_HOME="$test_root/state" DISPLAY=:98 \
	./superclip-clipboardd >"$test_root/duplicate.log" 2>&1
duplicate_status=$?
set -e
test "$duplicate_status" -ne 0
grep -F 'already running' "$test_root/duplicate.log"
XDG_RUNTIME_DIR="$test_root/runtime" DISPLAY=:98 ./superclip-clipboardd status |
	grep '^ok	0	'

kill -KILL "$daemon_pid"
wait "$daemon_pid" 2>/dev/null || true
daemon_pid=''
test -S "$test_root/runtime/superclip/clipboard.sock"
XDG_RUNTIME_DIR="$test_root/runtime" XDG_STATE_HOME="$test_root/state" DISPLAY=:98 \
	./superclip-clipboardd >>"$test_root/daemon.log" 2>&1 &
daemon_pid=$!
for _ in 1 2 3 4 5 6 7 8 9 10; do
	if XDG_RUNTIME_DIR="$test_root/runtime" DISPLAY=:98 ./superclip-clipboardd status >"$test_root/status" 2>/dev/null; then
		break
	fi
	sleep 1
done
grep '^ok	0	' "$test_root/status"

DISPLAY=:98 ./tests/clipboard_x11 --owner 'hello-世界' >"$test_root/owner.log" 2>&1 &
owner_pid=$!
query=''
item_id=''
for _ in 1 2 3 4 5 6 7 8 9 10; do
	query=$(printf 'QUERY\t1\t\n' | XDG_RUNTIME_DIR="$test_root/runtime" DISPLAY=:98 ./clipboard-extension --superclip-query 2>/dev/null || true)
	item_id=$(printf '%s\n' "$query" | awk -F '\t' '$1 == "ITEM" { print $3; exit }')
	if printf '%s\n' "$query" | grep -F 'hello-世界' >/dev/null 2>&1; then
		break
	fi
	sleep 1
done
if ! printf '%s\n' "$query" | grep -F 'hello-世界'; then
	cat "$test_root/owner.log" >&2
	cat "$test_root/daemon.log" >&2
	exit 1
fi
test -n "$item_id"
printf 'EXECUTE\t2\t\t%s\n' "$item_id" |
	XDG_RUNTIME_DIR="$test_root/runtime" DISPLAY=:98 ./clipboard-extension --superclip-execute |
	grep '^OK	2$'
kill "$owner_pid" 2>/dev/null || true
wait "$owner_pid" 2>/dev/null || true
owner_pid=''
DISPLAY=:98 ./tests/clipboard_x11 --request | grep -Fx 'hello-世界'
DISPLAY=:98 ./tests/clipboard_x11 --owner-incr 'incr-世界' >"$test_root/owner-incr.log" 2>&1 &
owner_pid=$!
query=''
for _ in 1 2 3 4 5 6 7 8 9 10; do
	query=$(printf 'QUERY\t3\t\n' | XDG_RUNTIME_DIR="$test_root/runtime" DISPLAY=:98 ./clipboard-extension --superclip-query 2>/dev/null || true)
	if printf '%s\n' "$query" | grep -F 'incr-世界' >/dev/null 2>&1; then
		break
	fi
	sleep 1
done
if ! printf '%s\n' "$query" | grep -F 'incr-世界'; then
	cat "$test_root/owner-incr.log" >&2
	cat "$test_root/daemon.log" >&2
	exit 1
fi
kill "$owner_pid" 2>/dev/null || true
wait "$owner_pid" 2>/dev/null || true
owner_pid=''
XDG_RUNTIME_DIR="$test_root/runtime" DISPLAY=:98 ./superclip-clipboardd clear | grep '^ok$'
kill -TERM "$daemon_pid"
wait "$daemon_pid"
daemon_pid=''
test ! -e "$test_root/runtime/superclip/clipboard.sock"
