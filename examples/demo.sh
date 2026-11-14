#!/usr/bin/env bash
# Live demo of the Mini Container Runtime, step by step.
#
#   sudo examples/demo.sh          pauses before every step (press Enter) - use this on stage
#   sudo examples/demo.sh --auto   no pauses, no waiting for the keyboard - use it to rehearse
#
# Run it from the repository root after building:
#   cmake -S . -B build && cmake --build build
#
# The resource-limit part needs cgroups v2 with the cpu, memory and pids
# controllers (default on current Ubuntu). Without them that part is skipped
# with a note and everything else still runs.

set -u

MCR="${MCR:-./build/mcr}"
ROOTFS="${ROOTFS:-./rootfs}"
AUTO=0
[ "${1:-}" = "--auto" ] && AUTO=1
[ -t 0 ] || AUTO=1                       # nobody at the keyboard

if [ -t 1 ]; then
    B=$'\033[1m'; G=$'\033[32m'; Y=$'\033[33m'; C=$'\033[36m'; R=$'\033[0m'
else
    B=""; G=""; Y=""; C=""; R=""
fi

if [ "$(id -u)" -ne 0 ]; then
    echo "The demo starts real containers, so it needs root: sudo $0 $*" >&2
    exit 2
fi
if [ ! -x "$MCR" ]; then
    echo "mcr not found at $MCR. Build it first: cmake -S . -B build && cmake --build build" >&2
    exit 2
fi

created_rootfs=0
demo_ids=""

# The demo keeps its container records in a private directory, so "mcr ps"
# only shows demo containers and your own containers are never touched.
export MCR_STATE_DIR
MCR_STATE_DIR="$(mktemp -d /tmp/mcr-demo-XXXXXX)"

cleanup() {
    for id in $demo_ids; do "$MCR" rm -f -t 1 "$id" >/dev/null 2>&1; done
    rm -rf "$MCR_STATE_DIR"
    [ "$created_rootfs" -eq 1 ] && rm -rf "$ROOTFS"
}
trap cleanup EXIT

# ---- helpers ---------------------------------------------------------------

step=0
pause() { [ "$AUTO" -eq 1 ] || read -r -p "${Y}   [Enter] to continue...${R}" _; }

title() {
    step=$((step + 1))
    echo
    echo "${B}${G}== Step $step: $1${R}"
}

say() { echo "${C}   $*${R}"; }

# show a command, then run it
run() {
    pause
    echo "${B}\$ $*${R}"
    eval "$@"
    echo
}

has_ctl() { case " $controllers " in *" $1 "*) return 0 ;; *) return 1 ;; esac; }

# ---- preparation -----------------------------------------------------------

echo "${B}Mini Container Runtime - live demo${R}"
echo "A container is just a normal Linux process that is given a private view of the system."

if [ ! -d "$ROOTFS" ]; then
    echo
    echo "Building a small root filesystem in $ROOTFS (only needed once)..."
    scripts/make-rootfs.sh "$ROOTFS" >/dev/null 2>&1 && created_rootfs=1 || {
        echo "could not build the rootfs" >&2; exit 2; }
fi

cg_root=/sys/fs/cgroup
[ -d /sys/fs/cgroup/unified ] && cg_root=/sys/fs/cgroup/unified
controllers="$(cat "$cg_root/cgroup.controllers" 2>/dev/null || true)"

# ---- 1. hostname (UTS namespace) --------------------------------------------

title "Hostname isolation (UTS namespace)"
say "The container gets its own hostname. The host's hostname must not change."
run "hostname"
run "$MCR run --hostname my-container -- hostname"
run "hostname"

# ---- 2. PID namespace ---------------------------------------------------------

title "Process isolation (PID namespace + /proc)"
say "Inside, the program is PID 1 and cannot see any host process."
run "echo \"host sees \$(ls /proc | grep -cE '^[0-9]+\$') processes\""
run "$MCR run -- sh -c 'echo \"my pid inside the container: \$\$\"; echo \"container sees \$(ls /proc | grep -cE \"^[0-9]+\$\") processes\"; ps'"

# ---- 3. root filesystem ------------------------------------------------------

title "Filesystem isolation (mount namespace + pivot_root)"
say "The container only sees the small rootfs, not the host's files. After pivot_root the old root is unmounted, so there is no way back."
run "ls /home | head -3"
run "$MCR run --rootfs $ROOTFS -- /bin/ls /"
run "$MCR run --rootfs $ROOTFS -- /bin/ls /home"
say "('No such file' is the point: the host's /home does not exist in there.)"

# ---- 4. resource limits ----------------------------------------------------------

title "Resource limits (cgroups v2)"
if has_ctl pids && has_ctl memory && has_ctl cpu; then
    say "Namespaces decide what a process can SEE; cgroups decide how much it can USE."

    say "Process limit: 5 processes allowed, we try to start 10."
    run "$MCR run --pids 5 -- sh -c 'for i in 1 2 3 4 5 6 7 8 9 10; do sleep 2 & done; wait' 2>&1 | head -4"

    say "Memory limit: 32 MB allowed, we try to hold 100 MB. The kernel kills it (exit status 137 = 128 + SIGKILL)."
    run "$MCR run --memory 32M -- sh -c 'x=\$(head -c 100000000 /dev/zero | tr \"\\0\" a); echo allocated \${#x}' ; echo \"exit status: \$?\""

    say "CPU limit: a busy loop that would use 100% of a core is held to 20%."
    pause
    echo "${B}\$ $MCR run -d --cpus 0.2 -- sh -c 'while :; do :; done'${R}"
    cpu_id="$("$MCR" run -d --cpus 0.2 -- sh -c 'while :; do :; done')"
    demo_ids="$demo_ids $cpu_id"
    echo "$cpu_id"
    sleep 3
    cpu_pid="$("$MCR" inspect "$cpu_id" | awk '/^Host PID/ {print $3}')"
    if [ -n "$cpu_pid" ]; then
        run "ps -o pid,%cpu,args -p $cpu_pid"
    else
        say "(the CPU-limited container did not start, check 'mcr ps')"
    fi
    run "$MCR rm -f -t 1 $cpu_id"
else
    say "SKIPPED: this machine has no cgroups v2 cpu/memory/pids controllers"
    say "(see $cg_root/cgroup.controllers). Show the CI run instead: all limit tests pass there."
fi

# ---- 5. lifecycle ------------------------------------------------------------

title "Container lifecycle: create, run, inspect, logs, stop, remove"
say "Every container gets an id and a status. Those started with -d run in the background."

# the earlier steps ran in the foreground and left finished records; start the list clean
rm -rf "${MCR_STATE_DIR:?}"/*

limit_args=""
has_ctl pids && has_ctl memory && has_ctl cpu && limit_args="--memory 64M --pids 20 --cpus 0.5"

pause
echo "${B}\$ $MCR run -d --hostname bg-box $limit_args -- sh -c 'while :; do echo working; sleep 1; done'${R}"
id="$("$MCR" run -d --hostname bg-box $limit_args -- sh -c 'while :; do echo working; sleep 1; done')"
demo_ids="$demo_ids $id"
echo "$id"
sleep 2.5

run "$MCR ps"
say "inspect shows the configuration and proves the namespaces are different from the host's."
run "$MCR inspect $id"
run "$MCR logs -n 3 $id"

say "stop sends SIGTERM first, then SIGKILL after the timeout. This shell loop ignores SIGTERM as PID 1, so it is killed."
run "$MCR stop -t 2 $id"
run "$MCR ps"
run "$MCR rm $id"
run "$MCR ps"

echo
echo "${B}${G}Demo finished.${R} Nothing is left running: processes, cgroups and records are cleaned up."
