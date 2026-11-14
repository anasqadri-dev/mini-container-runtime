# Verifying cgroups v2 limits

This document describes how we verify the `--memory`, `--pids` and `--cpus` limits.

## Environment
- OS / kernel: (fill in from `uname -a`)
- cgroup filesystem: (output of `stat -fc %T /sys/fs/cgroup`)
- Controllers: (output of `cat /sys/fs/cgroup/cgroup.controllers`)

## Test 1: memory limit
Command:
    sudo ./build/mcr run --rootfs ./rootfs --memory 64M -- \
      /bin/sh -c 'x=a; while true; do x="$x$x"; done'

Expected: the process is killed by the OOM killer once it passes 64 MB,
and `mcr` returns exit status 137 (128 + SIGKILL).
Result: (fill in)

## Test 2: process count limit
Command:
    sudo ./build/mcr run --rootfs ./rootfs --pids 8 -- \
      /bin/sh -c 'for i in 1 2 3 4 5 6 7 8 9 10 11 12; do sleep 30 & done; wait'

Expected: some forks fail with "Resource temporarily unavailable".
Result: (fill in)

## Test 3: CPU limit
Command (start a busy loop limited to 0.2 CPU, then look at the cgroup):
    id=$(sudo ./build/mcr run -d --cpus 0.2 -- /bin/sh -c 'while :; do :; done')
    sleep 3
    sudo cat /sys/fs/cgroup/mcr/mcr-*/cpu.max
    sudo cat /sys/fs/cgroup/mcr/mcr-*/cpu.stat
    sudo ./build/mcr rm -f $id

Expected: `cpu.max` is `20000 100000`, `usage_usec` is far below 3000000
(about 600000 after 3 seconds) and `nr_throttled` is greater than 0.
Using `top` in another terminal, the process stays near 20% of one core.
Result: (fill in)

## Test 4: no limit (control)
Run the same commands without `--memory` / `--pids` / `--cpus` and compare.

## Conclusion
(State whether each feature works, then update the README checkboxes.)

The same checks run automatically in `sudo tests/integration.sh`.