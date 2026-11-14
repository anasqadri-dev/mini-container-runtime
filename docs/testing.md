# Testing

There are two kinds of tests.

## Unit tests

They do not need root and run anywhere.

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

or without CMake:

```bash
make test
```

| Test          | Covers                                                          |
| ------------- | --------------------------------------------------------------- |
| `test_cli`    | `parse_size`, every option, `--` handling, error cases          |
| `test_util`   | `write_file`, `read_file`, `mkdir_p`                            |
| `test_state`  | Container records: ids, save/load round trip, prefix lookup, listing, removal, stale-record detection, rejection of malicious ids |
| `test_cgroup` | Which values are written to which control files, using a fake cgroup directory in `/tmp` |

The cgroup unit test cannot check real enforcement because the kernel
refuses to create cgroups in a temporary directory. That part is covered by
the integration tests.

## Integration tests

They start real containers and need root.

```bash
sudo tests/integration.sh
```

The script uses its own state directory (`MCR_STATE_DIR`), so it never touches
containers you started yourself. It builds a temporary rootfs with `scripts/make-rootfs.sh` (or uses
the one in `MCR_TEST_ROOTFS`) and checks:

- hostname isolation, PID 1, the `/proc` process count, no mount leaks
- the rootfs is visible, host directories are not, `/tmp` and `/dev` work
- exit statuses (`7`, `125`, `127`) and signal handling
- the full lifecycle: `run`, `ps`, `inspect`, `stop` (graceful and forced),
  `rm`, `rm -f`, `run --rm`, and recovery when the supervisor is killed
- `--pids` stops a fork bomb, `--memory` kills a program that allocates
  too much, and `--cpus` writes the right `cpu.max` and throttles a busy loop
- the cgroup directory is removed afterwards

Checks that need the cgroups v2 `memory` or `pids` controller are reported as
`skip` when the machine does not have them, for example inside some
containers or virtual machines with the hybrid cgroup layout. On Ubuntu
22.04 and newer they should run. To see what your machine offers:

```bash
cat /sys/fs/cgroup/cgroup.controllers
```

## Debugging

```bash
sudo ./build/mcr run -v --rootfs ./rootfs -- /bin/sh     # debug output from mcr
sudo strace -f -e trace=clone,mount,pivot_root,umount2 ./build/mcr run --rootfs ./rootfs -- /bin/true
sudo gdb --args ./build/mcr run -- /bin/true             # then: set follow-fork-mode child
```
