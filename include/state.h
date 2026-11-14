#ifndef MCR_STATE_H
#define MCR_STATE_H

#include <stddef.h>
#include <sys/types.h>
#include <time.h>

/* Container metadata kept on disk, one directory per container:
 *
 *   <state dir>/<id>/state        key=value file described by mcr_state_t
 *   <state dir>/<id>/output.log   stdout/stderr of detached containers
 *
 * The state directory is /run/mcr/containers unless MCR_STATE_DIR is set.
 * /run is a tmpfs, so state disappears on reboot, just like the containers. */

#define MCR_DEFAULT_STATE_DIR "/run/mcr/containers"
#define MCR_ID_LEN 8

typedef enum {
    STATUS_CREATED = 0,
    STATUS_RUNNING,
    STATUS_STOPPED
} mcr_status_t;

typedef struct {
    char         id[MCR_ID_LEN + 1];
    mcr_status_t status;
    pid_t        pid;          /* host PID of the container's PID 1, 0 if not started */
    pid_t        runtime_pid;  /* host PID of the mcr process supervising it */
    int          exit_code;    /* valid when STOPPED, -1 if unknown */
    char         command[256];
    char         hostname[65];
    char         rootfs[256];
    long long    memory_bytes;
    long         pids_max;
    long         cpu_millicores; /* 1000 = one CPU, 0 = unlimited */
    time_t       created;
    time_t       started;
    time_t       finished;
} mcr_state_t;

const char *status_name(mcr_status_t s);

/* Directory holding all container state (honours MCR_STATE_DIR). */
const char *state_dir(void);

/* Fill in a fresh record: new random id, status CREATED, created = now. */
int state_init(mcr_state_t *st);

/* Write the record to disk (atomically), creating the directory if needed. */
int state_save(const mcr_state_t *st);

/* Read the record for an exact id. */
int state_load(const char *id, mcr_state_t *out);

/* Find a container by full id or by unique prefix.
 * Returns 0 and fills out, -1 if nothing matches, -2 if the prefix is ambiguous. */
int state_find(const char *id_or_prefix, mcr_state_t *out);

/* Load every container, oldest first. Caller frees *list. Returns 0 or -1. */
int state_list(mcr_state_t **list, size_t *count);

/* Delete a container's metadata directory. */
int state_remove(const char *id);

/* Path of the per-container directory / log file (written into buf). */
void state_path(const char *id, const char *file, char *buf, size_t len);

/* If the record says RUNNING (or CREATED) but both the supervisor and the
 * container are gone, for example after a crash or kill -9 of mcr, mark it
 * STOPPED. Returns 1 if the record was changed. */
int state_refresh(mcr_state_t *st);

/* Join argv into a single display string such as "/bin/sh -c 'x'". */
void state_set_command(mcr_state_t *st, char *const *argv, int argc);

#endif /* MCR_STATE_H */
