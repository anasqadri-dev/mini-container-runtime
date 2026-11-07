#ifndef MCR_CGROUP_H
#define MCR_CGROUP_H

#include <sys/types.h>

/* Thin wrapper around the cgroups v2 filesystem interface.
 *
 * Layout used by the runtime:
 *   <root>/mcr/             parent group, controllers are enabled here
 *   <root>/mcr/<name>/      one group per container
 */

#define MCR_CGROUP_DEFAULT_ROOT "/sys/fs/cgroup"

typedef struct {
    char path[256];   /* absolute path of the container's cgroup directory */
    int  created;
} mcr_cgroup_t;

/* Override the cgroup mount point. Only meant for tests, where the "cgroup
 * filesystem" is a plain temporary directory. */
void cgroup_set_root(const char *root);

/* Returns 1 if the cgroup root is a cgroups v2 mount, 0 otherwise. */
int cgroup_is_v2(void);

/* Create the group <root>/mcr/<name> and enable the memory and pids
 * controllers for it. Returns 0 on success, -1 on error. */
int cgroup_create(mcr_cgroup_t *cg, const char *name);

/* memory.max (also disables swap and makes the OOM killer take the whole group) */
int cgroup_set_memory(const mcr_cgroup_t *cg, long long bytes);

/* pids.max */
int cgroup_set_pids(const mcr_cgroup_t *cg, long max);

/* Move a process into the group. */
int cgroup_add_pid(const mcr_cgroup_t *cg, pid_t pid);

/* Number of processes killed by the OOM killer in this group, -1 if unknown. */
long cgroup_oom_kills(const mcr_cgroup_t *cg);

/* Remove the group. It must be empty (all processes gone). Safe to call on a
 * group that was never created. */
int cgroup_destroy(mcr_cgroup_t *cg);

#endif /* MCR_CGROUP_H */
