#define _GNU_SOURCE
#include "commands.h"
#include "state.h"
#include "util.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define COMMAND_COLUMN_WIDTH 40

/* Find a container by id/prefix and bring its status up to date.
 * Prints the error itself; returns 0 on success, -1 on failure. */
static int lookup(const char *id, mcr_state_t *st)
{
    int rc = state_find(id, st);

    if (rc == -1) {
        LOG_ERR("no such container: %s", id);
        return -1;
    }
    if (rc == -2) {
        LOG_ERR("id prefix '%s' matches more than one container, use more characters", id);
        return -1;
    }
    if (state_refresh(st) == 1)
        state_save(st);   /* remember that a crashed container is stopped */
    return 0;
}

static void format_time(time_t t, char *buf, size_t len)
{
    if (t <= 0) {
        snprintf(buf, len, "-");
        return;
    }
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(buf, len, "%Y-%m-%d %H:%M:%S", &tm);
}

/* ---- ps ---------------------------------------------------------------- */

int cmd_ps(const mcr_options_t *opts)
{
    (void)opts;

    mcr_state_t *list = NULL;
    size_t n = 0;

    if (state_list(&list, &n) < 0) {
        LOG_ERR("cannot read container state in %s: %s", state_dir(), strerror(errno));
        return 1;
    }

    printf("%-14s %-8s %-14s %s\n", "CONTAINER ID", "PID", "STATUS", "COMMAND");
    printf("------------------------------------------------------------\n");

    for (size_t i = 0; i < n; i++) {
        mcr_state_t *st = &list[i];
        if (state_refresh(st) == 1)
            state_save(st);

        char pid[16] = "-";
        char status[32];
        char cmd[COMMAND_COLUMN_WIDTH + 4];

        if (st->status == STATUS_RUNNING)
            snprintf(pid, sizeof(pid), "%d", (int)st->pid);

        if (st->status == STATUS_STOPPED && st->exit_code >= 0)
            snprintf(status, sizeof(status), "%s (%d)", status_name(st->status), st->exit_code);
        else
            snprintf(status, sizeof(status), "%s", status_name(st->status));

        if (strlen(st->command) > COMMAND_COLUMN_WIDTH)
            snprintf(cmd, sizeof(cmd), "%.*s...", COMMAND_COLUMN_WIDTH - 3, st->command);
        else
            snprintf(cmd, sizeof(cmd), "%.*s", COMMAND_COLUMN_WIDTH, st->command);

        printf("%-14s %-8s %-14s %s\n", st->id, pid, status, cmd);
    }

    if (n == 0)
        printf("(no containers)\n");

    free(list);
    return 0;
}

/* ---- inspect ----------------------------------------------------------- */

static int read_link_text(const char *path, char *buf, size_t len)
{
    ssize_t n = readlink(path, buf, len - 1);
    if (n < 0)
        return -1;
    buf[n] = '\0';
    return 0;
}

/* Show that the container really lives in different namespaces than we do. */
static void print_namespaces(pid_t pid)
{
    static const char *names[] = {"pid", "uts", "mnt", "ipc"};

    printf("Namespaces:\n");
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        char path[64], mine[64], theirs[64];

        snprintf(path, sizeof(path), "/proc/self/ns/%s", names[i]);
        if (read_link_text(path, mine, sizeof(mine)) < 0)
            continue;
        snprintf(path, sizeof(path), "/proc/%d/ns/%s", (int)pid, names[i]);
        if (read_link_text(path, theirs, sizeof(theirs)) < 0)
            continue;

        printf("  %-4s %-22s %s\n", names[i], theirs,
               strcmp(mine, theirs) == 0 ? "(same as host)" : "(isolated)");
    }
}

/* Live usage from the container's cgroup, if there is one. */
static void print_usage(pid_t runtime_pid)
{
    static const char *roots[] = {"/sys/fs/cgroup", "/sys/fs/cgroup/unified"};

    for (size_t i = 0; i < sizeof(roots) / sizeof(roots[0]); i++) {
        char path[300], buf[64];
        int shown = 0;

        snprintf(path, sizeof(path), "%s/mcr/mcr-%d/memory.current", roots[i], (int)runtime_pid);
        if (read_file(path, buf, sizeof(buf)) > 0) {
            printf("Memory usage:  %lld bytes\n", atoll(buf));
            shown = 1;
        }
        snprintf(path, sizeof(path), "%s/mcr/mcr-%d/pids.current", roots[i], (int)runtime_pid);
        if (read_file(path, buf, sizeof(buf)) > 0) {
            printf("Processes:     %ld\n", atol(buf));
            shown = 1;
        }
        if (shown)
            return;
    }
}

int cmd_inspect(const mcr_options_t *opts)
{
    mcr_state_t st;
    char when[32];

    if (lookup(opts->id, &st) < 0)
        return 1;

    printf("Container:     %s\n", st.id);
    printf("Status:        %s\n", status_name(st.status));
    printf("Command:       %s\n", st.command);
    printf("Hostname:      %s\n", st.hostname);
    printf("Root fs:       %s\n", st.rootfs[0] ? st.rootfs : "(host filesystem)");

    if (st.memory_bytes > 0)
        printf("Memory limit:  %lld bytes (%lld MiB)\n", st.memory_bytes, st.memory_bytes >> 20);
    else
        printf("Memory limit:  none\n");
    if (st.pids_max > 0)
        printf("Process limit: %ld\n", st.pids_max);
    else
        printf("Process limit: none\n");

    format_time(st.created, when, sizeof(when));
    printf("Created:       %s\n", when);
    format_time(st.started, when, sizeof(when));
    printf("Started:       %s\n", when);

    if (st.status == STATUS_STOPPED) {
        format_time(st.finished, when, sizeof(when));
        printf("Finished:      %s\n", when);
        if (st.exit_code >= 0)
            printf("Exit code:     %d\n", st.exit_code);
        else
            printf("Exit code:     unknown (the runtime died)\n");
    }

    if (st.status == STATUS_RUNNING && st.pid > 0) {
        printf("Host PID:      %d\n", (int)st.pid);
        printf("Supervisor:    %d\n", (int)st.runtime_pid);
        print_usage(st.runtime_pid);
        print_namespaces(st.pid);
    }
    return 0;
}

/* ---- stop -------------------------------------------------------------- */

/* Poll until the container is no longer running. Returns 1 if it stopped. */
static int wait_stopped(const char *id, int seconds)
{
    struct timespec step = { .tv_sec = 0, .tv_nsec = 100 * 1000 * 1000 };

    for (int i = 0; i <= seconds * 10; i++) {
        mcr_state_t st;
        if (state_load(id, &st) < 0)
            return 1;   /* record vanished (--rm), so it is gone */
        state_refresh(&st);
        if (st.status != STATUS_RUNNING)
            return 1;
        nanosleep(&step, NULL);
    }
    return 0;
}

/* Stop a container that is known to be running. Returns 0 on success. */
static int stop_running(const mcr_state_t *st, int timeout)
{
    if (st->pid <= 0) {
        LOG_ERR("container %s has no process to stop", st->id);
        return -1;
    }

    LOG_DEBUG("sending SIGTERM to %d", (int)st->pid);
    if (kill(st->pid, SIGTERM) < 0 && errno != ESRCH) {
        LOG_ERR("cannot signal container %s: %s", st->id, strerror(errno));
        return -1;
    }

    if (timeout > 0 && wait_stopped(st->id, timeout))
        return 0;

    /* SIGKILL cannot be ignored, not even by PID 1 of a namespace. */
    LOG_WARN("container %s did not stop in %ds, sending SIGKILL", st->id, timeout);
    if (kill(st->pid, SIGKILL) < 0 && errno != ESRCH) {
        LOG_ERR("cannot kill container %s: %s", st->id, strerror(errno));
        return -1;
    }

    if (!wait_stopped(st->id, 5)) {
        LOG_ERR("container %s is still running after SIGKILL", st->id);
        return -1;
    }
    return 0;
}

int cmd_stop(const mcr_options_t *opts)
{
    mcr_state_t st;

    if (lookup(opts->id, &st) < 0)
        return 1;

    if (st.status != STATUS_RUNNING) {
        printf("container %s is not running (%s)\n", st.id, status_name(st.status));
        return 0;
    }

    if (stop_running(&st, opts->stop_timeout) < 0)
        return 1;

    printf("%s\n", st.id);
    return 0;
}

/* ---- rm ---------------------------------------------------------------- */

int cmd_rm(const mcr_options_t *opts)
{
    mcr_state_t st;

    if (lookup(opts->id, &st) < 0)
        return 1;

    if (st.status == STATUS_RUNNING) {
        if (!opts->force) {
            LOG_ERR("container %s is running, stop it first or use 'rm -f'", st.id);
            return 1;
        }
        if (stop_running(&st, opts->stop_timeout) < 0)
            return 1;
    }

    if (state_remove(st.id) < 0 && errno != ENOENT) {
        LOG_ERR("cannot remove container %s: %s", st.id, strerror(errno));
        return 1;
    }
    printf("%s\n", st.id);
    return 0;
}
