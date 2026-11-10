#!/usr/bin/env bash
# Filesystem isolation: build a small rootfs and run a shell inside it.
# Run from the repository root after building:  sudo examples/03-rootfs.sh

set -e
MCR="${MCR:-./build/mcr}"
ROOTFS="${ROOTFS:-./rootfs}"

if [ ! -d "$ROOTFS" ]; then
    scripts/make-rootfs.sh "$ROOTFS"
fi

echo
echo "--- what the container sees at /:"
"$MCR" run --rootfs "$ROOTFS" -- /bin/ls /

echo
echo "--- the host's /home is not reachable:"
"$MCR" run --rootfs "$ROOTFS" -- /bin/ls /home || true

echo
echo "--- starting an interactive shell (type 'exit' to leave):"
"$MCR" run --rootfs "$ROOTFS" --hostname sandbox -- /bin/sh
