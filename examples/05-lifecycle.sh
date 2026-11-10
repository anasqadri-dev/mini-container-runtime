#!/usr/bin/env bash
# Full container lifecycle: create -> run -> inspect -> stop -> clean up.
# Run from the repository root after building:  sudo examples/05-lifecycle.sh

set -e
MCR="${MCR:-./build/mcr}"

echo "--- start a container in the background"
ID=$("$MCR" run -d --hostname lifecycle-demo -- sleep 300)
echo "container id: $ID"
sleep 0.5

echo
echo "--- list containers"
"$MCR" ps

echo
echo "--- inspect it (note the isolated namespaces)"
"$MCR" inspect "$ID"

echo
echo "--- stop it (sleep ignores SIGTERM as PID 1, so mcr falls back to SIGKILL after 2s)"
"$MCR" stop -t 2 "$ID" || true

echo
echo "--- it is now STOPPED, with its exit status"
"$MCR" ps

echo
echo "--- remove it"
"$MCR" rm "$ID"
"$MCR" ps
