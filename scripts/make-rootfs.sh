#!/usr/bin/env bash
# Build a tiny root filesystem that can be used with: mcr run --rootfs <dir>
#
# Usage: scripts/make-rootfs.sh [target-dir]      (default: ./rootfs)
#
# If a statically linked busybox is installed (apt install busybox-static) it
# is used, which gives the smallest rootfs. Otherwise a handful of common
# tools are copied from the host together with the shared libraries they need.

set -euo pipefail

TARGET="${1:-rootfs}"

if [ -e "$TARGET" ] && [ -n "$(ls -A "$TARGET" 2>/dev/null)" ]; then
    echo "error: '$TARGET' already exists and is not empty" >&2
    exit 1
fi

mkdir -p "$TARGET"/{bin,sbin,lib,lib64,usr/bin,usr/lib,etc,proc,dev,tmp,root,sys}
chmod 1777 "$TARGET/tmp"

copy_with_libs() {
    local bin="$1"
    local real
    real="$(readlink -f "$bin")"

    # keep the same path inside the rootfs (resolve usr-merge symlinks)
    install -D -m 0755 "$real" "$TARGET/bin/$(basename "$bin")"

    # copy every shared library the binary needs, at the same absolute path
    ldd "$real" 2>/dev/null | awk '
        /=> \// { print $3 }
        /^[ \t]*\// { print $1 }
    ' | while read -r lib; do
        [ -e "$lib" ] || continue
        install -D -m 0755 "$(readlink -f "$lib")" "$TARGET$lib"
    done
}

BUSYBOX="$(command -v busybox || true)"
if [ -n "$BUSYBOX" ] && ldd "$BUSYBOX" 2>&1 | grep -q "not a dynamic"; then
    echo "using static busybox: $BUSYBOX"
    install -m 0755 "$BUSYBOX" "$TARGET/bin/busybox"
    for applet in $("$BUSYBOX" --list); do
        ln -sf busybox "$TARGET/bin/$applet"
    done
else
    echo "busybox-static not found, copying tools from the host"
    TOOLS="sh bash ls cat echo ps hostname sleep mkdir rm id uname env true false
           grep head tail touch wc cp mv pwd mount df free kill dd seq yes tr cut"
    for t in $TOOLS; do
        path="$(type -P "$t" || true)"
        [ -n "$path" ] && copy_with_libs "$path"
    done
fi

# Some tools look these up, so keep them minimal but present.
echo "root:x:0:0:root:/root:/bin/sh" > "$TARGET/etc/passwd"
echo "root:x:0:" > "$TARGET/etc/group"
echo "mini-container" > "$TARGET/etc/issue"

echo "rootfs ready in $TARGET ($(du -sh "$TARGET" | cut -f1))"
echo "try:  sudo ./build/mcr run --rootfs $TARGET -- /bin/sh"
