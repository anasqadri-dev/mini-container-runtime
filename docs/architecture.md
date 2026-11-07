# Architecture

This document explains how `mcr run` turns a normal Linux process into a
container. There is no daemon and no image format: one process calls
`clone()` once, configures the child, and waits for it.

## Source layout

| File                | Responsibility                                              |
| ------------------- | ----------------------------------------------------------- |
| `src/main.c`        | Entry point, dispatches the parsed command                  |
| `src/cli.c`         | Argument parsing and `parse_size()` (`64M`, `1G`, ...)      |
| `src/container.c`   | `clone()`, parent/child synchronisation, signals, exit code |
| `src/fs.c`          | Private mounts, `pivot_root`, `/dev`, `/tmp`, `/proc`       |
| `src/cgroup.c`      | cgroups v2: create group, set limits, move pid, clean up    |
| `src/util.c`        | Logging and small file helpers                              |

`cli`, `fs`, `cgroup` and `util` are compiled into a static library
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
└── mcr/                       controllers +memory +pids enabled here
    └── mcr-<mcr pid>/         one group per container
        ├── memory.max         from --memory
        ├── memory.swap.max    0, so swap cannot be used to dodge the limit
        ├── memory.oom.group   1, the OOM killer takes down the whole group
        ├── pids.max           from --pids
        └── cgroup.procs       the container's PID 1 is written here
```

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
- No CPU limit (`cpu.max`), only memory and process count.
- Containers are foreground only: no `ps`, `stop` or `exec` subcommands.
