# Architecture

This document explains how `mcr run` turns a normal Linux process into a
container. There is no daemon and no image format: one process calls
`clone()` once, configures the child, and waits for it.

## Source layout

| File                | Responsibility                                              |
| ------------------- | ----------------------------------------------------------- |
| `src/main.c`        | Entry point, dispatches the parsed command                  |
| `src/cli.c`         | Argument parsing and `parse_size()` (`64M`, `1G`, ...)      |
| `src/container.c`   | `clone()`, parent/child synchronisation, signals, exit code, detach |
| `src/state.c`       | Container records on disk (id, status, pids, exit code)     |
| `src/commands.c`    | `ps`, `inspect`, `stop`, `rm`                               |
| `src/fs.c`          | Private mounts, `pivot_root`, `/dev`, `/tmp`, `/proc`       |
| `src/cgroup.c`      | cgroups v2: create group, set limits, move pid, clean up    |
| `src/util.c`        | Logging and small file helpers                              |

`cli`, `fs`, `cgroup`, `state`, `commands` and `util` are compiled into a static library
(`mcr_core`) so the unit tests can link against them.

## Life of a container

```text
 parent (host namespaces)                    child (new namespaces)
 ------------------------                    ----------------------
 check root, check --rootfs
 pipe()
 clone(NEWPID|NEWUTS|NEWNS|NEWIPC) ────────► starts as PID 1, blocks on pipe
 install signal forwarding
 create cgroup mcr-<pid>
 write memory.max / pids.max
 move child into the cgroup
 write "go" to pipe ──────────────────────► wakes up
                                             sethostname()
                                             mount / as MS_PRIVATE
                                             pivot_root into --rootfs (if given)
                                             mount /proc
                                             execvp(command)
 waitpid()  ◄──────────────────────────────  program runs, then exits
 remove cgroup
 return exit status
```

### Why the pipe?

The child must not run user code before it is inside its cgroup, otherwise a
program could fork or allocate memory before the limits exist. The child
therefore blocks on a pipe until the parent has finished the cgroup setup.
If the setup fails, the parent closes the pipe without writing and kills the
child, so the program never starts without the limits the user asked for.

## Container lifecycle and state

There is no daemon. Instead, every `mcr run` process supervises its own
container and keeps a small record on disk, so that `ps`, `inspect`, `stop`
and `rm` (separate short-lived `mcr` processes) can find it.

```text
        run                       process starts            exits / killed
  ──────────────►  CREATED  ───────────────────►  RUNNING  ──────────────►  STOPPED  ──► rm ──► gone
   record written              cgroup ready,                exit code saved
                               "go" sent to child
```

State lives in `/run/mcr/containers/<id>/` (override with `MCR_STATE_DIR`):

| File         | Content                                                              |
| ------------ | -------------------------------------------------------------------- |
| `state`      | `key=value` lines: status, host pid, supervisor pid, exit code, command, hostname, rootfs, limits, timestamps |
| `output.log` | stdout and stderr of a detached container                            |

Design notes:

- **Ids** are 8 random hex characters. Any unique prefix is accepted. Ids are
  validated strictly (8 hex characters) before they are turned into paths, so
  something like `mcr rm ../../etc` cannot escape the state directory.
- **Atomic updates:** the record is written to `state.tmp` and moved into place
  with `rename()`, so a reader never sees a half-written file.
- **`/run` is a tmpfs.** Records vanish on reboot, which matches reality
  because the containers are gone too.
- **Stale records:** if `mcr` is killed with `kill -9` it cannot write the final
  status. `ps`, `inspect`, `stop` and `rm` therefore check whether the supervisor
  and the container are still alive. If both are gone the record becomes
  `STOPPED` with an unknown exit code. If only the supervisor died the container
  is still reported `RUNNING` (it is), and `stop` can still kill it directly.
  Process ids are not guarded against reuse, which is acceptable for a
  teaching project.
- **`run -d`** forks once, the parent prints the id and exits, and the child
  calls `setsid()`, redirects its output to `output.log` and carries on as the
  supervisor.

### `stop`

1. Send `SIGTERM` to the container's PID 1 (its host PID is in the record).
2. Poll the record for up to `--time` seconds (default 10).
3. If it is still running, send `SIGKILL`.

Because PID 1 of a namespace ignores signals it has no handler for, a
program like `sleep` never reacts to `SIGTERM` and is always killed after the
timeout, which is the same thing Docker does. A program that traps `SIGTERM`
exits at once. `SIGKILL` from the host cannot be ignored.

## Namespaces

| Flag          | Effect                                                             |
| ------------- | ------------------------------------------------------------------ |
| `CLONE_NEWPID`| The program is PID 1 and cannot see host processes                 |
| `CLONE_NEWUTS`| `--hostname` changes only the container's hostname                 |
| `CLONE_NEWNS` | Mounts made inside the container are invisible to the host         |
| `CLONE_NEWIPC`| Separate System V IPC and POSIX message queues                     |

Network and user namespaces are not used yet, see the roadmap below.

## Filesystem isolation

1. `mount(NULL, "/", NULL, MS_REC|MS_PRIVATE)` stops mount events from
   propagating back to the host. Without this, systemd's shared mounts would
   make the container's `/proc` show up on the host.
2. The rootfs directory is bind-mounted onto itself, because `pivot_root`
   requires the new root to be a mount point.
3. `pivot_root(".", ".old_root")` swaps the roots, then the old root is
   unmounted with `MNT_DETACH` and its directory removed. After this the
   host filesystem is unreachable.
4. A small tmpfs is mounted on `/dev` with `null`, `zero`, `random`,
   `urandom` and `tty`, and another tmpfs on `/tmp`.
5. A fresh `proc` is mounted on `/proc`. Because it is mounted inside the new
   PID namespace, `ps` only shows the container's processes.

`chroot` is deliberately not used: a root process can escape a chroot, while
after `pivot_root` plus unmounting the old root there is nothing left to
escape to.

## Resource limits (cgroups v2)

```text
/sys/fs/cgroup/
└── mcr/                       controllers +cpu +memory +pids enabled here
    └── mcr-<mcr pid>/         one group per container
        ├── memory.max         from --memory
        ├── memory.swap.max    0, so swap cannot be used to dodge the limit
        ├── memory.oom.group   1, the OOM killer takes down the whole group
        ├── pids.max           from --pids
        ├── cpu.max            from --cpus, "<quota> <period>" in microseconds
        └── cgroup.procs       the container's PID 1 is written here
```

`--cpus 0.5` becomes `cpu.max = "50000 100000"`: the container may run for
50 ms in every 100 ms period, which is half a CPU. `--cpus 2` becomes
`"200000 100000"`. The period is fixed at 100 ms and the smallest allowed
value is `0.01` (1 ms per period), the kernel's minimum. CPU limits are
stored in the container record as millicores (1000 = one CPU), which avoids
floating point.

Each limit only needs its own controller: the runtime asks for `cpu`,
`memory` and `pids` but treats all three as optional, and a missing one is
reported only when that limit is requested, for example *"the cgroups v2 cpu
controller is not available"*.

The group is removed after the container exits. `rmdir` can briefly fail
with `EBUSY` while the kernel finishes tearing down the processes, so the
runtime retries for up to two seconds.

On machines with the older hybrid layout the v2 tree is found under
`/sys/fs/cgroup/unified`. A pure v1 system is not supported and the runtime
refuses to start when limits are requested there, instead of silently
ignoring them.

## Exit codes

| Code  | Meaning                                              |
| ----- | ---------------------------------------------------- |
| `N`   | The program exited with status `N`                   |
| `128+N` | The program was killed by signal `N` (137 = SIGKILL, e.g. OOM) |
| `125` | The runtime itself failed (bad rootfs, no cgroup, ...)|
| `126` | The command was found but could not be executed      |
| `127` | The command was not found                            |

These follow the conventions used by `docker run`.

## Signals

`SIGINT`, `SIGTERM`, `SIGHUP` and `SIGQUIT` received by `mcr` are forwarded
to the container. The kernel ignores signals for PID 1 of a namespace unless
the program installed a handler, so a program like `sleep` survives the first
signal. A second signal is therefore turned into `SIGKILL`.

## Known limitations / roadmap

- No network namespace yet (the container shares the host network).
- No user namespace; the container runs as real root, so it is **not** a
  security boundary. Use it for learning, not for untrusted code.
- No capability dropping or seccomp filtering.
- No `exec` or `logs` subcommand; the log of a detached container is the file
  `/run/mcr/containers/<id>/output.log`.
- A container cannot be restarted; start a new one with `run`.
