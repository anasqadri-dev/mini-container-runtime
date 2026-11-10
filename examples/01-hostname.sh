#!/usr/bin/env bash
# UTS namespace: the container gets its own hostname.
# Run from the repository root after building:  sudo examples/01-hostname.sh

MCR="${MCR:-./build/mcr}"

echo "host hostname:      $(hostname)"
echo -n "container hostname: "
"$MCR" run --hostname my-container -- hostname
echo "host hostname after: $(hostname)"
