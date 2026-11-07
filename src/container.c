#define _GNU_SOURCE
#include "container.h"
#include "cgroup.h"
#include "fs.h"
#include "util.h"

#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#define STACK_SIZE (1024 * 1024)
#define EXIT_RUNTIME_ERROR 125   /* same convention as "docker run" */
#define EXIT_CANNOT_EXEC   126
#define EXIT_NOT_FOUND     127

/* Everything the child needs, passed through clone()'s void* argument. */
struct child_args {
    const mcr_options_t *opts;
    int sync_fd;   /* read end of the pipe the parent uses to say "go" */
};

/* Runs as PID 1 inside the new namespaces. */
static int child_main(void *arg)
{
    struct child_args *ca = arg;
    const mcr_options_t *o = ca->opts;
    char go;

    /* Wait until the parent has put us into our cgroup, so the limits apply
     * before the contained program runs its first instruction. If the parent
     * fails it closes the pipe without writing and we simply exit. */
    if (read(ca->sync_fd, &go, 1) != 1)
        return EXIT_RUNTIME_ERROR;
    close(ca->sync_fd);

    if (sethostname(o->hostname, strlen(o->hostname)) < 0) {
        LOG_ERR("sethostname failed: %s", strerror(errno));
        return EXIT_RUNTIME_ERROR;
    }

    if (fs_make_mounts_private() < 0)
        return EXIT_RUNTIME_ERROR;

    if (o->rootfs && fs_setup_rootfs(o->rootfs) < 0)
        return EXIT_RUNTIME_ERROR;

    if (fs_mount_proc() < 0)
        return EXIT_RUNTIME_ERROR;

    LOG_DEBUG("container started, pid inside namespace = %d", getpid());

    execvp(o->cmd_argv[0], o->cmd_argv);

    int saved = errno;
    LOG_ERR("cannot execute '%s': %s", o->cmd_argv[0], strerror(saved));
    return saved == ENOENT ? EXIT_NOT_FOUND : EXIT_CANNOT_EXEC;
}

/* PID of the running container, used by the signal handler to forward signals.
 * Written once before the handlers are installed. */
static volatile pid_t g_child_pid;
static volatile sig_atomic_t g_signals_seen;

static void forward_signal(int sig)
{
    if (g_child_pid <= 0)
        return;

    /* The kernel ignores signals sent to PID 1 of a namespace unless it has a
     * handler for them, so a plain "sleep" would shrug off a single Ctrl-C.
     * The first signal is forwarded as is; if the user insists, kill it. */
    if (g_signals_seen++ > 0)
        kill(g_child_pid, SIGKILL);
    else
        kill(g_child_pid, sig);
}

/* Ctrl-C and "kill" sent to mcr should stop the container too, instead of
 * leaving it running without a parent. */
static void install_signal_forwarding(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = forward_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
}

static void restore_signal_defaults(void)
{
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    signal(SIGHUP, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
}

/* Apply the limits from the command line to a fresh cgroup and move pid in. */
static int setup_cgroup(mcr_cgroup_t *cg, const mcr_options_t *opts, pid_t pid)
{
    char name[64];
    snprintf(name, sizeof(name), "mcr-%d", (int)getpid());

    if (!cgroup_is_v2()) {
        LOG_ERR("cgroups v2 is not mounted at %s (resource limits unavailable)",
                MCR_CGROUP_DEFAULT_ROOT);
        return -1;
    }
    if (cgroup_create(cg, name) < 0)
        return -1;
    if (opts->memory_bytes > 0 && cgroup_set_memory(cg, opts->memory_bytes) < 0)
        return -1;
    if (opts->pids_max > 0 && cgroup_set_pids(cg, opts->pids_max) < 0)
        return -1;
    return cgroup_add_pid(cg, pid);
}

int container_run(const mcr_options_t *opts)
{
    if (geteuid() != 0) {
        LOG_ERR("creating namespaces needs root privileges, try: sudo mcr run ...");
        return EXIT_RUNTIME_ERROR;
    }

    if (opts->rootfs && fs_check_rootfs(opts->rootfs) < 0)
        return EXIT_RUNTIME_ERROR;

    int pipefd[2];
    if (pipe2(pipefd, O_CLOEXEC) < 0) {
        LOG_ERR("pipe failed: %s", strerror(errno));
        return EXIT_RUNTIME_ERROR;
    }

    /* The child runs on its own stack. */
    char *stack = mmap(NULL, STACK_SIZE, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if (stack == MAP_FAILED) {
        LOG_ERR("cannot allocate child stack: %s", strerror(errno));
        close(pipefd[0]);
        close(pipefd[1]);
        return EXIT_RUNTIME_ERROR;
    }

    struct child_args ca = { .opts = opts, .sync_fd = pipefd[0] };
    int flags = CLONE_NEWPID | CLONE_NEWUTS | CLONE_NEWNS | CLONE_NEWIPC | SIGCHLD;

    /* the stack grows downwards on x86 and ARM, so pass the top */
    pid_t pid = clone(child_main, stack + STACK_SIZE, flags, &ca);
    if (pid < 0) {
        LOG_ERR("clone failed: %s", strerror(errno));
        munmap(stack, STACK_SIZE);
        close(pipefd[0]);
        close(pipefd[1]);
        return EXIT_RUNTIME_ERROR;
    }
    LOG_DEBUG("child created, host pid = %d", pid);
    close(pipefd[0]);

    g_child_pid = pid;
    install_signal_forwarding();

    int result = EXIT_RUNTIME_ERROR;
    mcr_cgroup_t cg = { .created = 0 };
    int limits = opts->memory_bytes > 0 || opts->pids_max > 0;
    int aborted = 0;

    if (limits && setup_cgroup(&cg, opts, pid) < 0) {
        /* Not starting without the limits the user asked for. Closing the
         * pipe makes the child exit, then we reap it below. */
        close(pipefd[1]);
        kill(pid, SIGKILL);
        aborted = 1;
    } else {
        char go = 'g';
        if (write(pipefd[1], &go, 1) != 1)
            LOG_WARN("could not start container: %s", strerror(errno));
        close(pipefd[1]);
    }

    int status = 0;
    int got_status = 0;
    while (!got_status) {
        if (waitpid(pid, &status, 0) >= 0) {
            got_status = 1;
        } else if (errno != EINTR) {
            LOG_ERR("waitpid failed: %s", strerror(errno));
            break;
        }
    }

    g_child_pid = 0;
    restore_signal_defaults();

    if (got_status && !aborted) {
        if (WIFEXITED(status)) {
            result = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            result = 128 + WTERMSIG(status);
            if (opts->memory_bytes > 0 && WTERMSIG(status) == SIGKILL &&
                cg.created && cgroup_oom_kills(&cg) > 0)
                LOG_ERR("container was killed: memory limit exceeded");
        }
    }

    /* PID 1 of the namespace is gone, so the kernel killed everything else in
     * it too. The cgroup is now empty and can be removed. */
    cgroup_destroy(&cg);
    munmap(stack, STACK_SIZE);
    return result;
}
