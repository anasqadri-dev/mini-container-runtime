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

- [ ] Command-line interface for container execution.
- [ ] Container process creation using Linux system calls.
- [ ] Process isolation using PID namespaces.
- [ ] Hostname isolation using UTS namespaces.
- [ ] Mount namespace isolation.
- [ ] Separate filesystem environment.
- [ ] Memory resource limits using cgroups v2.
- [ ] Process count limits using cgroups v2.
- [ ] Process information through procfs.
- [ ] Container lifecycle management and cleanup.

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

### 3. Run the project

The execution command and runtime configuration will be documented once the CLI implementation is ready.

**Note:** Container execution may require appropriate Linux capabilities and cgroups configuration, depending on the implemented features.

## Development Team

Developed by BS Software Engineering students at Sukkur IBA University.

| Name                | CMS ID      |
| ------------------- | ----------- |
| Muhammad Anas Qadri | 053-24-0016 |
| Masood Ahmed        | 053-24-0011 |
| Shaheer Ahmed       | 053-24-0028 |

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
