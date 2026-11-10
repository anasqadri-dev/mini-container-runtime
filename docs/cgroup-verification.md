# Verifying cgroups v2 limits

This document describes how we verify the `--memory` and `--pids` limits.

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

## Test 3: no limit (control)
Run the same commands without `--memory` / `--pids` and compare.

## Conclusion
(State whether each feature works, then update the README checkboxes.)