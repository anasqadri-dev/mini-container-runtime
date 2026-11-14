#define _GNU_SOURCE
#include "state.h"
#include "util.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

const char *status_name(mcr_status_t s)
{
    switch (s) {
    case STATUS_CREATED: return "CREATED";
    case STATUS_RUNNING: return "RUNNING";
    case STATUS_STOPPED: return "STOPPED";
    }
    return "UNKNOWN";
}

static int parse_status(const char *text, mcr_status_t *out)
{
    for (int s = STATUS_CREATED; s <= STATUS_STOPPED; s++) {
        if (strcmp(text, status_name((mcr_status_t)s)) == 0) {
            *out = (mcr_status_t)s;
            return 0;
        }
    }
    return -1;
}

const char *state_dir(void)
{
    const char *env = getenv("MCR_STATE_DIR");
    return (env && *env) ? env : MCR_DEFAULT_STATE_DIR;
}

void state_path(const char *id, const char *file, char *buf, size_t len)
{
    if (file && *file)
        snprintf(buf, len, "%s/%s/%s", state_dir(), id, file);
    else
        snprintf(buf, len, "%s/%s", state_dir(), id);
}

/* An id is MCR_ID_LEN lowercase hex characters. Anything else found in the
 * state directory (or typed by the user) is rejected, which also rules out
 * path tricks like "../". */
static int valid_id(const char *id)
{
    if (strlen(id) != MCR_ID_LEN)
        return 0;
    for (const char *p = id; *p; p++)
        if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f')))
            return 0;
    return 1;
}

static int random_id(char *out)
{
    unsigned char raw[MCR_ID_LEN / 2];
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return -1;
    ssize_t n = read(fd, raw, sizeof(raw));
    close(fd);
    if (n != (ssize_t)sizeof(raw))
        return -1;

    for (size_t i = 0; i < sizeof(raw); i++)
        snprintf(out + i * 2, 3, "%02x", raw[i]);
    return 0;
}

int state_init(mcr_state_t *st)
{
    memset(st, 0, sizeof(*st));

    /* a collision is unlikely (2^32) but cheap to rule out */
    for (int tries = 0; tries < 10; tries++) {
        if (random_id(st->id) < 0)
            return -1;
        char dir[512];
        state_path(st->id, NULL, dir, sizeof(dir));
        struct stat sb;
        if (stat(dir, &sb) < 0) {
            st->status = STATUS_CREATED;
            st->exit_code = -1;
            st->created = time(NULL);
            return 0;
        }
    }
    errno = EEXIST;
    return -1;
}

void state_set_command(mcr_state_t *st, char *const *argv, int argc)
{
    size_t used = 0;

    st->command[0] = '\0';
    for (int i = 0; i < argc; i++) {
        int n = snprintf(st->command + used, sizeof(st->command) - used,
                         "%s%s", i ? " " : "", argv[i]);
        if (n < 0 || (size_t)n >= sizeof(st->command) - used)
            break;
        used += (size_t)n;
    }
    /* the file format is line based */
    for (char *p = st->command; *p; p++)
        if (*p == '\n' || *p == '\r')
            *p = ' ';
}

int state_save(const mcr_state_t *st)
{
    char dir[512], tmp[560], path[560];

    state_path(st->id, NULL, dir, sizeof(dir));
    if (mkdir_p(dir, 0755) < 0)
        return -1;

    snprintf(tmp, sizeof(tmp), "%s/state.tmp", dir);
    snprintf(path, sizeof(path), "%s/state", dir);

    FILE *f = fopen(tmp, "w");
    if (!f)
        return -1;

    fprintf(f, "id=%s\n", st->id);
    fprintf(f, "status=%s\n", status_name(st->status));
    fprintf(f, "pid=%d\n", (int)st->pid);
    fprintf(f, "runtime_pid=%d\n", (int)st->runtime_pid);
    fprintf(f, "exit_code=%d\n", st->exit_code);
    fprintf(f, "command=%s\n", st->command);
    fprintf(f, "hostname=%s\n", st->hostname);
    fprintf(f, "rootfs=%s\n", st->rootfs);
    fprintf(f, "memory_bytes=%lld\n", st->memory_bytes);
    fprintf(f, "pids_max=%ld\n", st->pids_max);
    fprintf(f, "cpu_millicores=%ld\n", st->cpu_millicores);
    fprintf(f, "created=%lld\n", (long long)st->created);
    fprintf(f, "started=%lld\n", (long long)st->started);
    fprintf(f, "finished=%lld\n", (long long)st->finished);

    if (fflush(f) != 0 || ferror(f)) {
        fclose(f);
        unlink(tmp);
        return -1;
    }
    fclose(f);

    /* rename is atomic, so a reader never sees a half written file */
    if (rename(tmp, path) < 0) {
        unlink(tmp);
        return -1;
    }
    return 0;
}

static void copy_str(char *dst, size_t dstlen, const char *src)
{
    snprintf(dst, dstlen, "%s", src);
}

int state_load(const char *id, mcr_state_t *out)
{
    char path[560], line[512];

    if (!valid_id(id)) {
        errno = EINVAL;
        return -1;
    }

    snprintf(path, sizeof(path), "%s/%s/state", state_dir(), id);
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;

    memset(out, 0, sizeof(*out));
    out->exit_code = -1;
    int seen_id = 0;

    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = '\0';
        char *eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = '\0';
        const char *key = line, *val = eq + 1;

        if (strcmp(key, "id") == 0) {
            copy_str(out->id, sizeof(out->id), val);
            seen_id = 1;
        } else if (strcmp(key, "status") == 0) {
            if (parse_status(val, &out->status) < 0)
                out->status = STATUS_STOPPED;
        } else if (strcmp(key, "pid") == 0) {
            out->pid = (pid_t)atoi(val);
        } else if (strcmp(key, "runtime_pid") == 0) {
            out->runtime_pid = (pid_t)atoi(val);
        } else if (strcmp(key, "exit_code") == 0) {
            out->exit_code = atoi(val);
        } else if (strcmp(key, "command") == 0) {
            copy_str(out->command, sizeof(out->command), val);
        } else if (strcmp(key, "hostname") == 0) {
            copy_str(out->hostname, sizeof(out->hostname), val);
        } else if (strcmp(key, "rootfs") == 0) {
            copy_str(out->rootfs, sizeof(out->rootfs), val);
        } else if (strcmp(key, "memory_bytes") == 0) {
            out->memory_bytes = atoll(val);
        } else if (strcmp(key, "pids_max") == 0) {
            out->pids_max = atol(val);
        } else if (strcmp(key, "cpu_millicores") == 0) {
            out->cpu_millicores = atol(val);
        } else if (strcmp(key, "created") == 0) {
            out->created = (time_t)atoll(val);
        } else if (strcmp(key, "started") == 0) {
            out->started = (time_t)atoll(val);
        } else if (strcmp(key, "finished") == 0) {
            out->finished = (time_t)atoll(val);
        }
    }
    fclose(f);

    if (!seen_id || strcmp(out->id, id) != 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int state_find(const char *prefix, mcr_state_t *out)
{
    size_t plen = strlen(prefix);

    if (plen == 0 || plen > MCR_ID_LEN) {
        errno = ENOENT;
        return -1;
    }

    DIR *d = opendir(state_dir());
    if (!d) {
        errno = ENOENT;
        return -1;
    }

    char match[MCR_ID_LEN + 1] = "";
    int count = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!valid_id(e->d_name) || strncmp(e->d_name, prefix, plen) != 0)
            continue;
        count++;
        copy_str(match, sizeof(match), e->d_name);
        if (strcmp(e->d_name, prefix) == 0) {   /* exact id beats prefixes */
            count = 1;
            break;
        }
    }
    closedir(d);

    if (count == 0) {
        errno = ENOENT;
        return -1;
    }
    if (count > 1)
        return -2;
    return state_load(match, out);
}

static int by_created(const void *a, const void *b)
{
    const mcr_state_t *x = a, *y = b;
    if (x->created != y->created)
        return x->created < y->created ? -1 : 1;
    return strcmp(x->id, y->id);
}

int state_list(mcr_state_t **list, size_t *count)
{
    *list = NULL;
    *count = 0;

    DIR *d = opendir(state_dir());
    if (!d)
        return errno == ENOENT ? 0 : -1;   /* no directory yet = no containers */

    size_t cap = 8, n = 0;
    mcr_state_t *arr = malloc(cap * sizeof(*arr));
    if (!arr) {
        closedir(d);
        return -1;
    }

    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!valid_id(e->d_name))
            continue;
        if (n == cap) {
            cap *= 2;
            mcr_state_t *bigger = realloc(arr, cap * sizeof(*arr));
            if (!bigger) {
                free(arr);
                closedir(d);
                return -1;
            }
            arr = bigger;
        }
        if (state_load(e->d_name, &arr[n]) == 0)
            n++;
    }
    closedir(d);

    qsort(arr, n, sizeof(*arr), by_created);
    *list = arr;
    *count = n;
    return 0;
}

int state_remove(const char *id)
{
    char dir[512], path[800];

    if (!valid_id(id)) {
        errno = EINVAL;
        return -1;
    }
    state_path(id, NULL, dir, sizeof(dir));

    DIR *d = opendir(dir);
    if (!d)
        return -1;

    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        unlink(path);
    }
    closedir(d);

    return rmdir(dir);
}

static int process_alive(pid_t pid)
{
    if (pid <= 0)
        return 0;
    return kill(pid, 0) == 0 || errno == EPERM;
}

int state_refresh(mcr_state_t *st)
{
    if (st->status == STATUS_STOPPED)
        return 0;

    /* A record without any pid yet (the runtime is still starting) is not stale. */
    if (st->runtime_pid <= 0 && st->pid <= 0)
        return 0;

    if (process_alive(st->runtime_pid) || process_alive(st->pid))
        return 0;

    st->status = STATUS_STOPPED;
    st->exit_code = -1;
    if (st->finished == 0)
        st->finished = time(NULL);
    return 1;
}
