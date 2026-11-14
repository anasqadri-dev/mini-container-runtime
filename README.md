# Mini Container Runtime

A lightweight Linux-based container runtime implemented in C, demonstrating process isolation, filesystem isolation, and resource management using Linux namespaces and cgroups v2.

## About the Project

**Mini Container Runtime** is an Operating Systems Lab project developed as part of CS251 at Sukkur IBA University.

The project aims to demonstrate how fundamental Linux operating system mechanisms can be used to create isolated environments for running programs.

Users will be able to launch a program inside a basic container through a command-line interface. The runtime will use Linux namespaces to isolate processes and system resources, provide a separate filesystem environment, and use Linux cgroups v2 to apply basic resource limits.

The project demonstrates concepts behind modern container technologies such as Docker while implementing their fundamental mechanisms from scratch.

## Objectives

- Understand Linux process creation and management.
- Implement process isolation using Linux namespaces.
- Establish a separate filesystem environment for containers.
- Apply resource limits using Linux cgroups v2.
- Explore Linux system calls and low-level OS functionality.
- Understand how container technologies work internally.

## Planned Features

- [x] Command-line interface for container execution.
- [x] Container process creation using Linux system calls.
- [x] Process isolation using PID namespaces.
- [x] Hostname isolation using UTS namespaces.
- [x] Mount namespace isolation.
- [x] Separate filesystem environment.
- [x] Memory resource limits using cgroups v2.
- [x] CPU limits using cgroups v2.
- [x] Process count limits using cgroups v2.
- [x] Process information through procfs.
- [x] Container lifecycle management and cleanup (`ps`, `inspect`, `logs`, `stop`, `rm`).

_Features will be marked complete as they are implemented and tested._

## Technology Stack

| Technology         | Purpose                               |
| ------------------ | ------------------------------------- |
| C                  | Main programming language             |
| Linux / Ubuntu     | Development and execution platform    |
| GCC                | C compiler                            |
| CMake              | Build system                          |
| Linux Namespaces   | Process and system resource isolation |
| Linux cgroups v2   | Resource management                   |
| Linux system calls | Low-level OS operations               |
| procfs             | Process information                   |
| GDB                | Debugging                             |
| strace             | System call tracing                   |
| Git & GitHub       | Version control and collaboration     |

## Project Structure

```text
mini-container-runtime/
├── docs/
├── include/
├── src/
├── tests/
├── scripts/
├── examples/
├── CMakeLists.txt
├── Makefile
├── .gitignore
├── LICENSE
└── README.md
```

## Prerequisites

- Linux-based operating system (Ubuntu recommended).
- GCC compiler.
- CMake.
- GNU Make.
- Git.
- Linux kernel with namespace support.
- cgroups v2 support.

GDB and strace are recommended for debugging and system call analysis.

## Getting Started

### 1. Clone the repository

```bash
git clone https://github.com/anasqadri-dev/mini-container-runtime.git
cd mini-container-runtime
```

### 2. Build the project

```bash
cmake -S . -B build
cmake --build build
```

### 3. Run the tests

```bash
ctest --test-dir build --output-on-failure   # unit tests, no root needed
sudo tests/integration.sh                    # starts real containers, needs root
```

See [docs/testing.md](docs/testing.md) for details.

### 4. Run a container

Creating namespaces needs root.

```bash
# new hostname + PID namespace, still using the host's files
sudo ./build/mcr run --hostname demo -- /bin/sh

# full filesystem isolation: build a small rootfs first
scripts/make-rootfs.sh ./rootfs
sudo ./build/mcr run --rootfs ./rootfs -- /bin/sh

# with resource limits (cgroups v2)
sudo ./build/mcr run --rootfs ./rootfs --memory 64M --pids 32 --cpus 0.5 -- /bin/sh
```

### 5. Manage containers

Containers started with `-d` run in the background. Every container gets an
id and a status (`CREATED` -> `RUNNING` -> `STOPPED`).

```bash
id=$(sudo ./build/mcr run -d --rootfs ./rootfs -- /bin/sleep 300)

sudo ./build/mcr ps               # list containers
sudo ./build/mcr inspect $id      # details, limits, namespaces, live usage
sudo ./build/mcr logs $id         # output of a detached container
sudo ./build/mcr logs -f $id      # keep following it until it exits
sudo ./build/mcr stop $id         # SIGTERM, then SIGKILL after 10 seconds
sudo ./build/mcr rm $id           # delete the record of a stopped container
```

```text
CONTAINER ID   PID      STATUS         COMMAND
------------------------------------------------------------
a81f42c7       8231     RUNNING        /bin/sleep 300
5dcf2e55       -        STOPPED (137)  sleep 300
```

Ids can be shortened to any unique prefix (`mcr stop a81`).

| Option                 | Description                                         |
| ---------------------- | --------------------------------------------------- |
| `-r, --rootfs <dir>`   | Root filesystem of the container                    |
| `-n, --hostname <name>`| Hostname inside the container (default `container`) |
| `-m, --memory <size>`  | Memory limit, e.g. `64M`, `1G`                      |
| `-p, --pids <n>`       | Maximum number of processes                         |
| `-c, --cpus <n>`       | CPU limit, e.g. `0.5` (half a CPU) or `2`           |
| `-d, --detach`         | Run in the background and print the container id    |
| `--rm`                 | Remove the container's record when it exits         |
| `-v, --verbose`        | Debug output                                        |

| Command                      | Description                                            |
| ---------------------------- | ------------------------------------------------------ |
| `mcr ps`                     | List containers with id, PID, status and command       |
| `mcr inspect <id>`           | Show configuration, timestamps, exit code, namespaces  |
| `mcr logs [-f] [-n N] <id>`  | Show a detached container's output (`-f` follows, `-n` shows the last N lines) |
| `mcr stop [-t secs] <id>`    | SIGTERM, then SIGKILL after the timeout (default 10)   |
| `mcr rm [-f] [-t secs] <id>` | Remove a stopped container (`-f` stops it first)       |

Everything after the options (optionally after `--`) is the program to run.
The exit status of `mcr` is the exit status of that program. More examples
are in the [examples](examples/) folder, `sudo examples/demo.sh` runs a guided
live demo (see [docs/demo.md](docs/demo.md)), and the design is explained in
[docs/architecture.md](docs/architecture.md).

> **Warning:** the container runs as real root and has no network, user or
> seccomp isolation. It is a learning project, not a security boundary.

## Development Team

Developed by BS Software Engineering students at Sukkur IBA University.

| Name                | CMS ID      |
| ------------------- | ----------- |
| Muhammad Anas Qadri | 053-24-0016 |
| Masood Ahmed        | 053-24-0011 |
| Shaheer Ahmed Siddiqui      | 053-24-0038 |

## Academic Information

- **University:** Sukkur IBA University
- **Department:** Computer Science
- **Course:** CS251 – Operating System Lab
- **Program:** BS Software Engineering
- **Semester:** BS-V
- **Section:** A
- **Session:** Fall 2026
- **Instructor:** Sir Abbas Mehdi

## Learning Outcomes

This project provides practical experience with:

- Process management and creation.
- Linux namespaces and isolation.
- Filesystem management.
- Resource allocation and control.
- Linux system calls.
- Low-level systems programming.

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details, subject to course and team requirements.

---

**Mini Container Runtime** — Understanding the fundamentals behind Linux containers.
