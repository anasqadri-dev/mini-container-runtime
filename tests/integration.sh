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

# Keep the tests' container records away from the real ones in /run/mcr.
export MCR_STATE_DIR
MCR_STATE_DIR="$(mktemp -d /tmp/mcr-state-XXXXXX)"

cleanup() {
    for id in $(ls "$MCR_STATE_DIR" 2>/dev/null); do "$MCR" rm -f -t 1 "$id" >/dev/null 2>&1; done
    rm -rf "$MCR_STATE_DIR"
    [ -z "${MCR_TEST_ROOTFS:-}" ] && rm -rf "$ROOTFS"
}
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

echo "== container lifecycle (create, run, inspect, stop, clean up)"

# records left by the sections above would confuse the checks below
rm -rf "${MCR_STATE_DIR:?}"/*

# a foreground container leaves a STOPPED record with its exit code
"$MCR" run -- sh -c 'exit 5'
fg_id="$(ls "$MCR_STATE_DIR" | head -1)"
[ -n "$fg_id" ] && ok "run records a container id ($fg_id)" || bad "no container record created"

out="$("$MCR" ps)"
echo "$out" | grep "$fg_id" | grep -q "STOPPED (5)" \
    && ok "ps shows the finished container with its exit code" || bad "ps output" "$out"

out="$("$MCR" inspect "$fg_id")"
echo "$out" | grep -q "^Exit code:     5" && ok "inspect shows the exit code" || bad "inspect exit code" "$out"
"$MCR" rm "$fg_id" >/dev/null && [ ! -e "$MCR_STATE_DIR/$fg_id" ] \
    && ok "rm deletes a stopped container" || bad "rm of stopped container"

"$MCR" run --rm -- true
[ -z "$(ls "$MCR_STATE_DIR")" ] && ok "--rm removes the record when the container exits" || bad "--rm left a record"

# detached container: CREATED -> RUNNING -> inspect -> STOPPED -> removed
id="$("$MCR" run -d --hostname bg-box -- sleep 300)"
sleep 0.5
if [ "${#id}" -eq 8 ]; then ok "run -d prints the container id ($id)"; else bad "run -d output" "got '$id'"; fi

"$MCR" ps | grep "$id" | grep -q "RUNNING" && ok "ps lists the container as RUNNING" || bad "ps does not show RUNNING"

out="$("$MCR" inspect "${id:0:3}")"
echo "$out" | grep -q "^Status:        RUNNING" && ok "inspect works with an id prefix" || bad "inspect prefix" "$out"
echo "$out" | grep -q "^Hostname:      bg-box" && ok "inspect shows the hostname" || bad "inspect hostname"
n_iso="$(echo "$out" | grep -c '(isolated)')"
[ "$n_iso" -eq 4 ] && ok "inspect shows PID, UTS, mount and IPC namespaces as isolated" \
    || bad "namespaces not isolated" "isolated=$n_iso"

host_pid="$(grep '^pid=' "$MCR_STATE_DIR/$id/state" | cut -d= -f2)"
[ -d "/proc/$host_pid" ] && ok "container PID 1 has host PID $host_pid" || bad "host pid not alive"

"$MCR" rm "$id" 2>/dev/null; rc=$?
[ $rc -ne 0 ] && ok "rm refuses to remove a running container" || bad "rm removed a running container"

"$MCR" stop -t 1 "$id" >/dev/null 2>&1
"$MCR" ps | grep "$id" | grep -q "STOPPED" && ok "stop: container is STOPPED afterwards" || bad "stop did not stop"
[ ! -d "/proc/$host_pid" ] && ok "stop: the container process is gone" || bad "container process survived stop"

# a program that handles SIGTERM exits cleanly and fast, no SIGKILL needed
id="$("$MCR" run -d -- sh -c 'trap "exit 0" TERM; while :; do sleep 0.1; done')"
sleep 0.5
start=$(date +%s)
"$MCR" stop "$id" >/dev/null 2>&1
took=$(( $(date +%s) - start ))
code="$("$MCR" inspect "$id" | awk '/^Exit code/ {print $3}')"
[ "$code" = "0" ] && [ "$took" -lt 5 ] && ok "stop: SIGTERM lets a well-behaved program exit with 0 (${took}s)" \
    || bad "graceful stop" "exit=$code took=${took}s"

# a program that ignores TERM has to be killed after the timeout
id="$("$MCR" run -d -- sleep 300)"
sleep 0.5
"$MCR" stop -t 1 "$id" >/dev/null 2>&1
code="$("$MCR" inspect "$id" | awk '/^Exit code/ {print $3}')"
[ "$code" = "137" ] && ok "stop: falls back to SIGKILL (exit 137) after the timeout" || bad "forced stop" "exit=$code"

"$MCR" stop "$id" | grep -q "not running" && ok "stop on a stopped container is harmless" || bad "double stop"

id="$("$MCR" run -d -- sleep 300)"
sleep 0.5
"$MCR" rm -f -t 1 "$id" >/dev/null 2>&1
[ ! -e "$MCR_STATE_DIR/$id" ] && pgrep -x sleep >/dev/null && bad "rm -f left a process" || ok "rm -f stops and removes a running container"

# supervisor killed by accident: the record must not stay RUNNING forever
id="$("$MCR" run -d -- sleep 300)"
sleep 0.5
sup="$(grep '^runtime_pid=' "$MCR_STATE_DIR/$id/state" | cut -d= -f2)"
kill -9 "$sup"; sleep 0.3
"$MCR" stop -t 1 "$id" >/dev/null 2>&1
"$MCR" ps | grep "$id" | grep -q "STOPPED" && ok "a container whose supervisor was killed can still be stopped" || bad "orphan container"

# logs: output of a detached container is kept and can be read back
id="$("$MCR" run -d -- sh -c 'echo first; echo second; echo third; sleep 300')"
sleep 0.5
out="$("$MCR" logs "$id")"
[ "$out" = "$(printf 'first\nsecond\nthird')" ] && ok "logs shows the output of a detached container" || bad "logs output" "got '$out'"
out="$("$MCR" logs -n 2 "$id")"
[ "$out" = "$(printf 'second\nthird')" ] && ok "logs --tail 2 shows only the last two lines" || bad "logs --tail" "got '$out'"
out="$("$MCR" logs -n 0 "$id")"
[ -z "$out" ] && ok "logs --tail 0 shows nothing" || bad "logs --tail 0" "got '$out'"
"$MCR" rm -f -t 1 "$id" >/dev/null 2>&1

# logs -f keeps going while the container runs and stops when it exits,
# without losing the last line
id="$("$MCR" run -d -- sh -c 'for i in 1 2 3; do echo tick$i; sleep 0.3; done; echo finished')"
out="$(timeout 15 "$MCR" logs -f "$id")"; rc=$?
[ $rc -eq 0 ] && [ "$(echo "$out" | tail -1)" = "finished" ] && [ "$(echo "$out" | wc -l)" -eq 4 ] \
    && ok "logs --follow prints everything and returns when the container exits" \
    || bad "logs --follow" "rc=$rc output='$out'"

start=$(date +%s)
timeout 5 "$MCR" logs -f "$id" >/dev/null
[ $(( $(date +%s) - start )) -lt 3 ] && ok "logs --follow on a stopped container returns at once" || bad "follow on stopped container hangs"

"$MCR" run -- true
fg="$(ls -t "$MCR_STATE_DIR" | head -1)"
"$MCR" logs "$fg" >/dev/null 2>&1; rc=$?
[ $rc -eq 1 ] && ok "logs on a foreground container explains there is no saved output" || bad "logs on foreground container" "rc=$rc"

"$MCR" inspect nosuchid >/dev/null 2>&1; rc=$?
[ $rc -eq 1 ] && ok "unknown container id is an error" || bad "unknown id" "rc=$rc"

[ -z "$(pgrep -x sleep)" ] && ok "no container processes are left behind" || bad "leftover sleep processes"

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

if has_ctl cpu; then
    # the limit must reach the kernel as "<quota> <period>"
    id="$("$MCR" run -d --cpus 0.5 -- sleep 300)"
    sleep 0.5
    sup="$(grep '^runtime_pid=' "$MCR_STATE_DIR/$id/state" | cut -d= -f2)"
    value="$(cat "$cg_root/mcr/mcr-$sup/cpu.max" 2>/dev/null)"
    [ "$value" = "50000 100000" ] && ok "cpu.max is '50000 100000' for --cpus 0.5" || bad "cpu.max value" "got '$value'"
    "$MCR" inspect "$id" | grep -q "^CPU limit:     0.5" && ok "inspect shows the CPU limit" || bad "inspect CPU limit"
    "$MCR" rm -f -t 1 "$id" >/dev/null 2>&1

    # and it must actually throttle: a busy loop limited to 0.2 CPU may use
    # about 0.6 s of CPU time in 3 s of wall time (allow generous slack)
    id="$("$MCR" run -d --cpus 0.2 -- sh -c 'while :; do :; done')"
    sleep 3
    sup="$(grep '^runtime_pid=' "$MCR_STATE_DIR/$id/state" | cut -d= -f2)"
    used="$(awk '/^usage_usec/ {print $2}' "$cg_root/mcr/mcr-$sup/cpu.stat" 2>/dev/null)"
    throttled="$(awk '/^nr_throttled/ {print $2}' "$cg_root/mcr/mcr-$sup/cpu.stat" 2>/dev/null)"
    "$MCR" rm -f -t 1 "$id" >/dev/null 2>&1
    if [ -n "$used" ] && [ "$used" -lt 1500000 ] && [ "${throttled:-0}" -gt 0 ]; then
        ok "cpu limit throttles a busy loop (used $((used / 1000)) ms CPU in 3 s, throttled $throttled times)"
    else
        bad "cpu limit not enforced" "usage_usec=$used nr_throttled=$throttled"
    fi
else
    skp "cpu limit" "cpu controller not available in cgroup v2"
fi

if has_ctl memory || has_ctl pids || has_ctl cpu; then
    left="$(ls "$cg_root/mcr" 2>/dev/null | grep -c '^mcr-')"
    [ "$left" = "0" ] && ok "cgroups are removed after the container exits" || bad "$left leftover cgroups"
else
    skp "cgroup cleanup" "no usable controllers"
fi

echo
echo "$pass passed, $fail failed, $skip skipped"
[ "$fail" -eq 0 ]
