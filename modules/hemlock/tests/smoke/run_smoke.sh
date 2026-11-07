#!/usr/bin/env bash
# Run the Hemlock smoke project headless and check its output.
# Usage: modules/hemlock/tests/smoke/run_smoke.sh [path/to/godot-binary]
set -u
here="$(cd "$(dirname "$0")" && pwd)"
godot="${1:-$(ls "$here"/../../../../bin/godot.* 2>/dev/null | head -1)}"
if [ -z "$godot" ] || [ ! -x "$godot" ]; then
    echo "usage: $0 path/to/godot-binary" >&2
    exit 2
fi

out="$(timeout 120 "$godot" --headless --path "$here" 2>&1)"
status=0
expect() {
    if ! grep -qF -- "$1" <<<"$out"; then
        echo "MISSING: $1"
        status=1
    fi
}

expect "READY Mover speed=200.0"
expect "CHILD Sprite2D true"
expect "GD_PROPS 200.0 mover"
expect "GD_PING 5 7"
expect "GD_SET 50.0 true"
expect "GD_TIMER true"
expect "SCRIPT ERROR: boom from hemlock"
expect "at: Hemlock: in explode() (res://mover.hml:39)"
expect "GD_ALIVE"
expect "ARRIVED 3 Vector2"
expect "SIGNAL true"
expect "BUMP 1 11 [1, 11]"
expect 'NOTES ["ready-notification"]'
expect "MATH [1, 9.0]"
expect "RNG [6, true]"
expect "cannot set 'no_such_prop' on Node"
expect "(res://counter.hml:31)"
expect "BROKEN false"
expect "(res://broken.hml:1)"
expect "UNDEFINED false"
expect "undefined name 'nope_not_defined'"
expect "THROWS_TOP true"
expect "top-level failure 1"
expect "EXT_RELOAD true"
expect "EXT_REJECTED true"
expect "HOT_RELOAD true 11 111"
expect "MOVED true"

if [ $status -ne 0 ]; then
    echo "---- output ----"
    echo "$out"
    exit 1
fi
echo "hemlock smoke test: all checks passed"
