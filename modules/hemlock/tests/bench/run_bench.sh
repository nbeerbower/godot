#!/usr/bin/env bash
# _process cost, Hemlock vs GDScript: ns per call over 1000 nodes, best of 5.
# Usage: modules/hemlock/tests/bench/run_bench.sh [path/to/godot-binary] [nodes]
here="$(cd "$(dirname "$0")" && pwd)"
godot="${1:-$(ls "$here"/../../../../bin/godot.* 2>/dev/null | head -1)}"
nodes="${2:-1000}"
for s in empty.gd empty.hml compute.gd compute.hml engine.gd engine_dynamic.gd engine.hml; do
    rounds=200
    [[ $s == engine.hml ]] && rounds=20
    "$godot" --headless --path "$here" -- "$s" "$nodes" "$rounds" 2>&1 | grep RESULT
done
