#!/bin/sh
# Xvfb integration for snippets: query/execute/holder/single-instance/stop.
set -eu

if ! command -v Xvfb >/dev/null 2>&1; then
	echo "Xvfb is required for snippets integration tests" >&2
	exit 1
fi

test_root=$(mktemp -d "${TMPDIR:-/tmp}/superclip-snippets.XXXXXX")
cleanup() {
	if [ "${holder_pid:-}" ]; then
		kill "$holder_pid" 2>/dev/null || true
		wait "$holder_pid" 2>/dev/null || true
	fi
	if [ "${xvfb_pid:-}" ]; then
		kill "$xvfb_pid" 2>/dev/null || true
		wait "$xvfb_pid" 2>/dev/null || true
	fi
	rm -rf "$test_root"
}
trap cleanup EXIT HUP INT TERM

mkdir -p "$test_root/config/superclip" "$test_root/runtime"
chmod 700 "$test_root/runtime"
printf 'hello-snippet\tHello World\nsecond\tSecond Body Line1\\nLine2\n' \
	> "$test_root/config/superclip/snippets"
chmod 600 "$test_root/config/superclip/snippets"

Xvfb :99 -screen 0 1024x768x24 >"$test_root/xvfb.log" 2>&1 &
xvfb_pid=$!
for _ in 1 2 3 4 5 6 7 8 9 10; do
	if DISPLAY=:99 ./tests/test_x11 >/dev/null 2>&1; then
		break
	fi
	sleep 1
done

export XDG_CONFIG_HOME="$test_root/config"
export XDG_RUNTIME_DIR="$test_root/runtime"
export DISPLAY=:99

query=$(printf 'QUERY\t1\t\n' | ./snippets-extension --superclip-query)
printf '%s\n' "$query" | grep -F 'ITEM	1	1:hello-snippet	hello-snippet	Hello World'
printf '%s\n' "$query" | grep -F 'ITEM	1	2:second	second	Second Body Line1'
item_id=$(printf '%s\n' "$query" | awk -F '\t' '$1 == "ITEM" { print $3; exit }')
test -n "$item_id"

printf 'EXECUTE\t2\t\t%s\n' "$item_id" | ./snippets-extension --superclip-execute |
	grep '^OK	2$'
holder_pid=$(awk '{ print $1; exit }' "$test_root/runtime/superclip/snippets-holder.pid")
test -n "$holder_pid"
kill -0 "$holder_pid"

# Session is gone (short mode); holder must still serve the paste.
DISPLAY=:99 ./tests/clipboard_x11 --request | grep -Fx 'Hello World'

# Second execute replaces the holder: PID changes, old PID dies.
printf 'second\tSecond Body Line1\\nLine2\nthird\tThird\n' \
	> "$test_root/config/superclip/snippets"
query2=$(printf 'QUERY\t3\tsecond\n' | ./snippets-extension --superclip-query)
item2=$(printf '%s\n' "$query2" | awk -F '\t' '$1 == "ITEM" { print $3; exit }')
printf 'EXECUTE\t4\tsecond\t%s\n' "$item2" | ./snippets-extension --superclip-execute |
	grep '^OK	4$'
new_pid=$(awk '{ print $1; exit }' "$test_root/runtime/superclip/snippets-holder.pid")
test -n "$new_pid"
test "$new_pid" != "$holder_pid"
if kill -0 "$holder_pid" 2>/dev/null; then
	echo "old holder still alive" >&2
	exit 1
fi
holder_pid=$new_pid
DISPLAY=:99 ./tests/clipboard_x11 --request | grep -Fx 'Second Body Line1
Line2'

# Explicit stop: no process, no pid file, no owner.
./snippets-extension --holder-stop
test ! -e "$test_root/runtime/superclip/snippets-holder.pid"
if kill -0 "$holder_pid" 2>/dev/null; then
	echo "holder still alive after stop" >&2
	exit 1
fi
holder_pid=''
if DISPLAY=:99 ./tests/clipboard_x11 --request >/dev/null 2>&1; then
	echo "selection still owned after stop" >&2
	exit 1
fi
echo "snippets-xvfb: PASS"
