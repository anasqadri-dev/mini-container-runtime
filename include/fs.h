#ifndef MCR_FS_H
#define MCR_FS_H

/* Stop mount events inside the container from leaking back to the host.
 * Must be called first thing inside the new mount namespace.
 * Returns 0 on success, -1 on error. */
int fs_make_mounts_private(void);

/* Mount a fresh procfs on /proc so tools like ps only see the container's
 * own PID namespace. Returns 0 on success, -1 on error. */
int fs_mount_proc(void);

#endif /* MCR_FS_H */
