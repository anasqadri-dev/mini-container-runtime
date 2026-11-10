#!/usr/bin/env bash
# PID namespace + procfs: the container only sees its own processes and its
# main program is PID 1.
# Run from the repository root after building:  sudo examples/02-pid-namespace.sh

MCR="${MCR:-./build/mcr}"

echo "processes visible on the host: $(ls /proc | grep -cE '^[0-9]+$')"
echo "inside the container:"
"$MCR" run -- sh -c 'echo "  my pid is $$"; echo "  processes visible: $(ls /proc | grep -cE "^[0-9]+$")"; ps 2>/dev/null || true'
