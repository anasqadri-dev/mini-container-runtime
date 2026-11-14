#define _GNU_SOURCE
#include "cgroup.h"
#include "util.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <time.h>
#include <unistd.h>

#ifndef CGROUP2_SUPER_MAGIC
#define CGROUP2_SUPER_MAGIC 0x63677270
#endif

static char root_path[256] = MCR_CGROUP_DEFAULT_ROOT;
static int  root_overridden;

void cgroup_set_root(const char *root)
{
    snprintf(root_path, sizeof(root_path), "%s", root);
    root_overridden = 1;
}

static int is_v2_mount(const char *path)
{
    struct statfs sfs;

    if (statfs(path, &sfs) < 0)
        return 0;
    return sfs.f_type == CGROUP2_SUPER_MAGIC;
}

int cgroup_is_v2(void)
{
    if (is_v2_mount(root_path))
        return 1;

    /* Hybrid layout (older systemd setups): the v2 tree is mounted under
     * <root>/unified while <root> itself holds the v1 controllers. */
    char unified[300];
    snprintf(unified, sizeof(unified), "%s/unified", root_path);
    if (is_v2_mount(unified) && strlen(unified) < sizeof(root_path)) {
        memcpy(root_path, unified, strlen(unified) + 1);
        LOG_DEBUG("hybrid cgroup layout, using %s", unified);
        return 1;
    }
    return 0;
}

/* Write a value into a cgroup control file.
 * The real cgroup filesystem creates its files itself. With a fake root (tests)
 * nothing does, so we create them on demand to imitate that. */
static int cg_write(const char *dir, const char *file, const char *value)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, file);

    if (root_overridden) {
        int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0)
            return -1;
        close(fd);
    }

    if (write_file(path, value) < 0) {
        LOG_DEBUG("cannot write '%s' to %s: %s", value, path, strerror(errno));
        return -1;
    }
    return 0;
}

/* Hand the controllers we may need down to the children of <dir>.
 *
 * All of them are best effort: each limit only needs its own controller, so
 * a machine without "pids" must still be able to run --cpus. If a limit is
 * requested and its controller is missing, the matching cgroup_set_* call
 * fails with an explanation. Writing one controller at a time matters: a
 * single unavailable one would otherwise make the kernel reject the line. */
static void enable_controllers(const char *dir)
{
    static const char *const wanted[] = {"+cpu", "+memory", "+pids"};

    for (size_t i = 0; i < sizeof(wanted) / sizeof(wanted[0]); i++) {
        if (cg_write(dir, "cgroup.subtree_control", wanted[i]) < 0)
            LOG_DEBUG("cannot enable %s in %s: %s", wanted[i], dir, strerror(errno));
    }
}

/* Explain why writing a limit failed, with a hint for the usual cause. */
static void limit_error(const char *what, const char *controller)
{
    if (errno == ENOENT)
        LOG_ERR("cannot set %s limit: the cgroups v2 %s controller is not available "
                "(see %s/cgroup.controllers)", what, controller, root_path);
    else
        LOG_ERR("cannot set %s limit: %s", what, strerror(errno));
}

int cgroup_create(mcr_cgroup_t *cg, const char *name)
{
    char parent[300];

    memset(cg, 0, sizeof(*cg));

    snprintf(parent, sizeof(parent), "%s/mcr", root_path);
    if (mkdir(parent, 0755) < 0 && errno != EEXIST) {
        LOG_ERR("cannot create cgroup %s: %s", parent, strerror(errno));
        return -1;
    }

    /* children of the root group can only get controllers if the root hands
     * them down first; failure is not fatal because they may already be on */
    enable_controllers(root_path);
    enable_controllers(parent);

    int n = snprintf(cg->path, sizeof(cg->path), "%s/%s", parent, name);
    if (n < 0 || (size_t)n >= sizeof(cg->path)) {
        LOG_ERR("cgroup name too long");
        return -1;
    }
    if (mkdir(cg->path, 0755) < 0) {
        LOG_ERR("cannot create cgroup %s: %s", cg->path, strerror(errno));
        return -1;
    }

    cg->created = 1;
    LOG_DEBUG("created cgroup %s", cg->path);
    return 0;
}

int cgroup_set_memory(const mcr_cgroup_t *cg, long long bytes)
{
    char val[32];
    snprintf(val, sizeof(val), "%lld", bytes);

    if (cg_write(cg->path, "memory.max", val) < 0) {
        limit_error("memory", "memory");
        return -1;
    }

    /* Best effort extras: without them a container could dodge the limit by
     * swapping, and the OOM killer might leave half of the group running. */
    (void)cg_write(cg->path, "memory.swap.max", "0");
    (void)cg_write(cg->path, "memory.oom.group", "1");
    return 0;
}

int cgroup_set_cpu(const mcr_cgroup_t *cg, long millicores)
{
    char val[48];

    if (millicores < 10) {
        errno = EINVAL;
        LOG_ERR("cpu limit too small");
        return -1;
    }

    /* quota = run time allowed per period, in microseconds */
    long quota = millicores * (MCR_CPU_PERIOD_US / 1000);
    snprintf(val, sizeof(val), "%ld %d", quota, MCR_CPU_PERIOD_US);

    if (cg_write(cg->path, "cpu.max", val) < 0) {
        limit_error("cpu", "cpu");
        return -1;
    }
    return 0;
}

int cgroup_set_pids(const mcr_cgroup_t *cg, long max)
{
    char val[32];
    snprintf(val, sizeof(val), "%ld", max);

    if (cg_write(cg->path, "pids.max", val) < 0) {
        limit_error("process", "pids");
        return -1;
    }
    return 0;
}

int cgroup_add_pid(const mcr_cgroup_t *cg, pid_t pid)
{
    char val[32];
    snprintf(val, sizeof(val), "%d", (int)pid);

    if (cg_write(cg->path, "cgroup.procs", val) < 0) {
        LOG_ERR("cannot move pid %d into cgroup: %s", (int)pid, strerror(errno));
        return -1;
    }
    return 0;
}

long cgroup_oom_kills(const mcr_cgroup_t *cg)
{
    char path[512], buf[1024];

    snprintf(path, sizeof(path), "%s/memory.events", cg->path);
    if (read_file(path, buf, sizeof(buf)) < 0)
        return -1;

    const char *p = strstr(buf, "oom_kill ");
    if (!p)
        return -1;
    return strtol(p + strlen("oom_kill "), NULL, 10);
}

/* In a fake tree the control files are real files and block rmdir. */
static void remove_fake_files(const char *dir)
{
    DIR *d = opendir(dir);
    if (!d)
        return;

    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_type != DT_REG)
            continue;
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        unlink(path);
    }
    closedir(d);
}

int cgroup_destroy(mcr_cgroup_t *cg)
{
    if (!cg->created)
        return 0;

    if (root_overridden)
        remove_fake_files(cg->path);

    /* The kernel needs a moment to finish tearing down the group's processes,
     * so rmdir can briefly report EBUSY. */
    struct timespec pause = { .tv_sec = 0, .tv_nsec = 50 * 1000 * 1000 };
    for (int tries = 0; tries < 40; tries++) {
        if (rmdir(cg->path) == 0) {
            cg->created = 0;
            LOG_DEBUG("removed cgroup %s", cg->path);
            return 0;
        }
        if (errno != EBUSY)
            break;
        nanosleep(&pause, NULL);
    }

    LOG_WARN("could not remove cgroup %s: %s", cg->path, strerror(errno));
    return -1;
}
