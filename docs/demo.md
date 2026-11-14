# Demo guide

How to present the Mini Container Runtime live. The demo takes about 8 to 10
minutes and is driven by one script:

```bash
sudo examples/demo.sh          # pauses before every step, press Enter to go on
sudo examples/demo.sh --auto   # no pauses, for rehearsing
```

The script keeps its own container records in a temporary directory, so
`mcr ps` only shows demo containers, and it cleans up after itself.

## Before you present

- [ ] Build it: `cmake -S . -B build && cmake --build build`
- [ ] Run `sudo -v` first so the password prompt does not appear mid-demo.
- [ ] Rehearse once with `sudo examples/demo.sh --auto` and read the output.
- [ ] Check the limits will work: `cat /sys/fs/cgroup/cgroup.controllers` must list
      `cpu`, `memory` and `pids`. If the file does not exist (older WSL setups),
      step 4 is skipped automatically. Say so, and show the passing CI run of
      the integration tests instead (GitHub, Actions tab). Every limit test is
      green there.
- [ ] Make the terminal font large and clear the screen (`clear`).
- [ ] Have a second terminal open on `docs/architecture.md` in case someone asks how it works.

## The idea in one sentence

> A container is not a small virtual machine. It is a normal Linux process that
> the kernel lets see only part of the system (**namespaces**) and use only part
> of the resources (**cgroups**).

Say this at the start and again at the end.

## Step by step

| Step | What the script shows | What to say | Point at |
| ---- | --------------------- | ----------- | -------- |
| 1 | `mcr run --hostname my-container -- hostname` | UTS namespace. The container has its own hostname and the host's is unchanged. | The host name before and after is the same. |
| 2 | PID namespace and `/proc` | The program thinks it is PID 1 and sees only itself. We mount a fresh `/proc` inside the new namespace, which is why `ps` agrees. | "host sees N processes" against "container sees 4" (your numbers will differ), and `my pid ... is 1`. |
| 3 | `--rootfs` and `ls /home` | Mount namespace plus `pivot_root`. The container gets its own root and the host's files do not exist there. | `No such file or directory` for `/home`. That error is the proof. |
| 4 | Process, memory and CPU limits | Namespaces control what a process can see, cgroups control how much it can use. | Forks failing, exit status 137 for the memory kill, `%CPU` around 20 for the busy loop. |
| 5 | `run -d`, `ps`, `inspect`, `logs`, `stop`, `rm` | The full lifecycle. State is kept in a small file per container so separate commands can find it. | `inspect` shows the namespaces marked `(isolated)`. `stop` falls back to SIGKILL. `ps` shows `STOPPED (137)`. |

Closing line: *"Nothing is left running. The processes, the cgroups and the records
are all cleaned up."*

## Suggested split between three people

| Person | Covers | Be ready to explain |
| ------ | ------ | ------------------- |
| 1 | Introduction, steps 1 to 3 | `clone()` and the `CLONE_NEW*` flags, why `pivot_root` and not `chroot`, why `/` is made private, why `/proc` is mounted again |
| 2 | Step 4 | cgroup v2 files (`memory.max`, `pids.max`, `cpu.max`), the pipe that makes the child wait until the limits exist, OOM kill and exit status 137 |
| 3 | Step 5, tests and CI | State files in `/run/mcr`, `stop` (SIGTERM then SIGKILL), stale-record detection, the unit and integration tests, the GitHub Actions run |

Read the matching part of [architecture.md](architecture.md) before presenting your part.

## Questions you may be asked

**How is this different from Docker?**
Docker adds a lot on top of the same kernel features: images and layers
(overlayfs), a daemon, networking, user namespaces, seccomp and capability
filtering. We implemented the core: namespaces, a separate root filesystem and
cgroup limits.

**Why `clone()` and not `fork()`?**
`fork()` makes a child in the same namespaces. `clone()` takes flags
(`CLONE_NEWPID`, `CLONE_NEWUTS`, `CLONE_NEWNS`, `CLONE_NEWIPC`) that put the
child into new ones from its first instruction.

**Why `pivot_root` and not `chroot`?**
`chroot` only changes where paths start. A root process can escape from it.
`pivot_root` swaps the root of the mount namespace and we then unmount the old
root with `MNT_DETACH`, so the host filesystem is no longer reachable at all.

**Why do you mount `/` as private first?**
On systemd machines `/` is shared, so our mounts would appear on the host too.
Making it private (`MS_REC | MS_PRIVATE`) keeps them inside the container.

**What is the difference between namespaces and cgroups?**
Namespaces limit what a process can *see* (processes, hostname, mounts, IPC).
Cgroups limit how much it can *use* (memory, CPU, number of processes).

**What does exit status 137 mean?**
128 + 9: the process was killed by signal 9 (SIGKILL). The kernel's OOM killer
does this when a container goes over `memory.max`. Our `stop` command also ends
with 137 when SIGTERM is not enough.

**Why does `stop` take the full timeout for `sleep`?**
The kernel ignores signals sent to PID 1 of a namespace unless the program has
a handler for them. `sleep` has none, so it ignores SIGTERM and is killed by
SIGKILL after the timeout, which cannot be ignored. A program that handles
SIGTERM, like the one in our integration test, exits immediately.

**Why does the child wait on a pipe before running the program?**
So the program cannot start before it is inside its cgroup. Otherwise it could
fork or allocate memory before the limits exist. If the cgroup setup fails, the
child is killed and the program never runs without the limits you asked for.

**What happens if `mcr` itself is killed?**
The container may keep running, and the record would say RUNNING forever.
`ps`, `inspect`, `logs`, `stop` and `rm` check whether the supervisor and the
container are still alive and correct the record. If only the supervisor is
dead, `stop` can still kill the container directly.

**Is it secure?**
No, and we say so. The container runs as real root, there is no user
namespace, no seccomp and no capability dropping, and it shares the host
network. It is for learning how containers work, not for running untrusted code.

**Where is the container state stored?**
In `/run/mcr/containers/<id>/`, which is a tmpfs, so it disappears on reboot,
just like the containers do. Each record is replaced atomically with `rename()`.

**What would you add next?**
A network namespace with a veth pair, a user namespace, seccomp filtering,
overlayfs for image layers and an `exec` command.

## If something goes wrong

| Problem | Fix |
| ------- | --- |
| `creating namespaces needs root privileges` | Run with `sudo`. |
| Step 4 says SKIPPED | The machine has no cgroups v2 controllers. Show the CI run. |
| A container is left over after a crash | `sudo ./build/mcr ps`, then `sudo ./build/mcr rm -f <id>`. The demo's own records are in `/tmp/mcr-demo-*`, which the script removes on exit. |
| A cgroup directory is left over | `sudo rmdir /sys/fs/cgroup/mcr/mcr-*` (or under `/sys/fs/cgroup/unified/mcr` on the hybrid layout). |
| `mcr: not found` | Build first, or run the script from the repository root. |
