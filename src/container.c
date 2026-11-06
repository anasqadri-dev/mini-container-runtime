#define _GNU_SOURCE
#include "container.h"
#include "fs.h"
#include "util.h"

#include <errno.h>
#include <sched.h>
#include <signal.h>
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
};

/* Runs as PID 1 inside the new namespaces. */
static int child_main(void *arg)
{
    struct child_args *ca = arg;
    const mcr_options_t *o = ca->opts;

    if (sethostname(o->hostname, strlen(o->hostname)) < 0) {
        LOG_ERR("sethostname failed: %s", strerror(errno));
        return EXIT_RUNTIME_ERROR;
    }

    if (fs_make_mounts_private() < 0)
        return EXIT_RUNTIME_ERROR;

    if (fs_mount_proc() < 0)
        return EXIT_RUNTIME_ERROR;

    LOG_DEBUG("container started, pid inside namespace = %d", getpid());

    execvp(o->cmd_argv[0], o->cmd_argv);

    int saved = errno;
    LOG_ERR("cannot execute '%s': %s", o->cmd_argv[0], strerror(saved));
    return saved == ENOENT ? EXIT_NOT_FOUND : EXIT_CANNOT_EXEC;
}

int container_run(const mcr_options_t *opts)
{
    if (geteuid() != 0) {
        LOG_ERR("creating namespaces needs root privileges, try: sudo mcr run ...");
        return EXIT_RUNTIME_ERROR;
    }

    /* The child runs on its own stack. mmap gives us a guard-friendly region
     * that is freed as soon as the child is gone. */
    char *stack = mmap(NULL, STACK_SIZE, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if (stack == MAP_FAILED) {
        LOG_ERR("cannot allocate child stack: %s", strerror(errno));
        return EXIT_RUNTIME_ERROR;
    }

    struct child_args ca = { .opts = opts };

    int flags = CLONE_NEWPID | CLONE_NEWUTS | CLONE_NEWNS | CLONE_NEWIPC | SIGCHLD;

    /* the stack grows downwards on x86 and ARM, so pass the top */
    pid_t pid = clone(child_main, stack + STACK_SIZE, flags, &ca);
    if (pid < 0) {
        LOG_ERR("clone failed: %s", strerror(errno));
        munmap(stack, STACK_SIZE);
        return EXIT_RUNTIME_ERROR;
    }
    LOG_DEBUG("child created, host pid = %d", pid);

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            LOG_ERR("waitpid failed: %s", strerror(errno));
            munmap(stack, STACK_SIZE);
            return EXIT_RUNTIME_ERROR;
        }
    }
    munmap(stack, STACK_SIZE);

    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    if (WIFSIGNALED(status))
        return 128 + WTERMSIG(status);
    return EXIT_RUNTIME_ERROR;
}
