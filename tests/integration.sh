#!/usr/bin/env bash
# End-to-end tests for mcr. They start real containers, so they need root:
#
#   sudo tests/integration.sh [path-to-mcr]
#
# Checks that need cgroups v2 with the memory/pids controllers are skipped
# (not failed) when the machine does not offer them.

set -u

MCR="${1:-./build/mcr}"
ROOTFS="${MCR_TEST_ROOTFS:-$(mktemp -d /tmp/mcr-rootfs-XXXXXX)}"
HERE="$(cd "$(dirname "$0")" && pwd)"

pass=0; fail=0; skip=0

ok()   { pass=$((pass + 1)); echo "  ok    - $1"; }
bad()  { fail=$((fail + 1)); echo "  FAIL  - $1"; [ -n "${2:-}" ] && echo "          $2"; }
skp()  { skip=$((skip + 1)); echo "  skip  - $1 ($2)"; }

if [ "$(id -u)" -ne 0 ]; then
    echo "integration tests need root, run with sudo" >&2
    exit 2
fi
if [ ! -x "$MCR" ]; then
    echo "mcr binary not found at $MCR (build it first)" >&2
    exit 2
fi

cleanup() { [ -z "${MCR_TEST_ROOTFS:-}" ] && rm -rf "$ROOTFS"; }
trap cleanup EXIT

if [ -z "${MCR_TEST_ROOTFS:-}" ]; then
    rmdir "$ROOTFS"
    "$HERE/../scripts/make-rootfs.sh" "$ROOTFS" >/dev/null || { echo "could not build rootfs"; exit 2; }
fi

echo "== namespaces"

host_name="$(hostname)"
out="$("$MCR" run --hostname boxed -- hostname 2>&1)"
[ "$out" = "boxed" ] && ok "UTS: hostname is changed inside the container" || bad "UTS hostname" "got '$out'"
[ "$(hostname)" = "$host_name" ] && ok "UTS: host hostname is untouched" || bad "host hostname changed"

out="$("$MCR" run -- sh -c 'echo $$' 2>&1)"
[ "$out" = "1" ] && ok "PID: contained process is PID 1" || bad "PID namespace" "got '$out'"

host_procs="$(ls /proc | grep -cE '^[0-9]+$')"
cont_procs="$("$MCR" run -- sh -c 'ls /proc | grep -cE "^[0-9]+$"' 2>&1)"
[ "$cont_procs" -lt "$host_procs" ] 2>/dev/null \
    && ok "procfs: container sees $cont_procs processes, host sees $host_procs" \
    || bad "procfs shows too many processes" "container=$cont_procs host=$host_procs"

before="$(grep -c ' /proc ' /proc/self/mountinfo)"
"$MCR" run -- true
after="$(grep -c ' /proc ' /proc/self/mountinfo)"
[ "$before" = "$after" ] && ok "mounts: nothing leaks into the host mount table" || bad "mount leak"

echo "== filesystem"

out="$("$MCR" run --rootfs "$ROOTFS" -- /bin/sh -c 'ls /' 2>&1 | tr '\n' ' ')"
case "$out" in
    *bin*etc*) ok "rootfs: contents of the new root are visible" ;;
    *) bad "rootfs listing" "got '$out'" ;;
esac

out="$("$MCR" run --rootfs "$ROOTFS" -- /bin/sh -c 'ls /home 2>&1; ls /root >/dev/null 2>&1 && echo has-root' 2>&1)"
case "$out" in
    *"No such file"*) ok "rootfs: host directories are not reachable" ;;
    *) bad "host /home visible inside container" "got '$out'" ;;
esac

out="$("$MCR" run --rootfs "$ROOTFS" -- /bin/sh -c 'echo data > /tmp/probe && cat /tmp/probe' 2>&1)"
[ "$out" = "data" ] && ok "rootfs: /tmp is writable" || bad "/tmp not writable" "got '$out'"
[ ! -e "$ROOTFS/tmp/probe" ] && ok "rootfs: /tmp is a private tmpfs" || bad "tmp file leaked into rootfs dir"

out="$("$MCR" run --rootfs "$ROOTFS" -- /bin/sh -c 'echo x > /dev/null && head -c 4 /dev/zero | wc -c' 2>&1)"
[ "$out" = "4" ] && ok "rootfs: /dev/null and /dev/zero work" || bad "/dev nodes" "got '$out'"

[ ! -e "$ROOTFS/.old_root" ] && ok "rootfs: old root mount point is cleaned up" || bad ".old_root left behind"

echo "== lifecycle"

"$MCR" run -- sh -c 'exit 7'; rc=$?
[ $rc -eq 7 ] && ok "exit status of the program is returned" || bad "exit status" "got $rc"

"$MCR" run -- no-such-program-xyz 2>/dev/null; rc=$?
[ $rc -eq 127 ] && ok "missing program returns 127" || bad "missing program" "got $rc"

"$MCR" run --rootfs /no/such/dir -- true 2>/dev/null; rc=$?
[ $rc -eq 125 ] && ok "missing rootfs returns 125" || bad "missing rootfs" "got $rc"

# PID 1 of a namespace ignores signals it has no handler for, so one TERM is
# not enough for "sleep"; a second one makes mcr fall back to SIGKILL.
"$MCR" run -- sleep 30 &
pid=$!
sleep 0.5
kill -TERM $pid; sleep 0.3; kill -TERM $pid
wait $pid 2>/dev/null; rc=$?
[ $rc -eq 137 ] && ok "repeated SIGTERM stops the container (exit 128+9)" || bad "signal handling" "got $rc"
sleep 0.2
pgrep -x sleep >/dev/null && bad "contained process survived" || ok "no process left behind after the container is stopped"

echo "== cgroups v2"

cg_root=/sys/fs/cgroup
[ -d /sys/fs/cgroup/unified ] && cg_root=/sys/fs/cgroup/unified
controllers="$(cat "$cg_root/cgroup.controllers" 2>/dev/null)"

has_ctl() { case " $controllers " in *" $1 "*) return 0 ;; *) return 1 ;; esac; }

if has_ctl pids; then
    out="$("$MCR" run --pids 5 -- sh -c 'cat /proc/self/cgroup >/dev/null; for i in 1 2 3 4 5 6 7 8; do sleep 2 & done 2>&1; wait' 2>&1 | grep -c -i 'fork\|resource\|cannot')"
    [ "$out" -ge 1 ] && ok "pids.max stops fork bombs" || bad "pids limit not enforced"
else
    skp "pids limit" "pids controller not available in cgroup v2"
fi

if has_ctl memory; then
    "$MCR" run --memory 32M -- sh -c 'x=$(head -c 100000000 /dev/zero | tr "\0" a); echo ${#x}' >/dev/null 2>&1; rc=$?
    [ $rc -ne 0 ] && ok "memory.max kills a program that allocates too much" || bad "memory limit not enforced"
else
    skp "memory limit" "memory controller not available in cgroup v2"
fi

if has_ctl memory || has_ctl pids; then
    left="$(ls "$cg_root/mcr" 2>/dev/null | grep -c '^mcr-')"
    [ "$left" = "0" ] && ok "cgroups are removed after the container exits" || bad "$left leftover cgroups"
else
    skp "cgroup cleanup" "no usable controllers"
fi

echo
echo "$pass passed, $fail failed, $skip skipped"
[ "$fail" -eq 0 ]
