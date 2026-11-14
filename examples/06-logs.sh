#!/usr/bin/env bash
# Reading a container's output with "mcr logs".
# Run from the repository root after building:  sudo examples/06-logs.sh

set -e
MCR="${MCR:-./build/mcr}"

echo "--- start a background container that prints a line every half second"
ID=$("$MCR" run -d -- sh -c 'for i in 1 2 3 4 5 6; do echo "step $i"; sleep 0.5; done; echo "all done"')
echo "container id: $ID"

sleep 1.2
echo
echo "--- what it has printed so far"
"$MCR" logs "$ID"

echo
echo "--- only the last 2 lines"
"$MCR" logs -n 2 "$ID"

echo
echo "--- follow it until it exits (like tail -f)"
"$MCR" logs -f "$ID"

echo
echo "--- clean up"
"$MCR" rm "$ID"
