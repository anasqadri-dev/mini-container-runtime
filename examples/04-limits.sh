#!/usr/bin/env bash
# Resource limits with cgroups v2.
# Needs a machine where cgroups v2 has the memory, pids and cpu controllers
# (the default on current Ubuntu). Check with:  cat /sys/fs/cgroup/cgroup.controllers
# Run from the repository root after building:  sudo examples/04-limits.sh

MCR="${MCR:-./build/mcr}"

echo "--- process limit: only 5 processes allowed, we try to start 10"
"$MCR" run --pids 5 -- sh -c 'for i in 1 2 3 4 5 6 7 8 9 10; do sleep 2 & done; wait'
echo "(the shell reports that it cannot fork once the limit is reached)"

echo
echo "--- memory limit: 32M allowed, we try to hold about 100M"
"$MCR" run --memory 32M -- sh -c 'x=$(head -c 100000000 /dev/zero | tr "\0" a); echo "allocated ${#x} bytes"'
echo "exit status: $? (137 means the kernel killed it for using too much memory)"

echo
echo "--- cpu limit: a busy loop limited to 0.2 CPU for 3 seconds"
ID=$("$MCR" run -d --cpus 0.2 -- sh -c 'while :; do :; done')
sleep 3
"$MCR" inspect "$ID" | grep -E "CPU limit|Status"
echo "(watch it with 'top' in another terminal: it stays near 20% of one core)"
"$MCR" rm -f -t 1 "$ID" > /dev/null
