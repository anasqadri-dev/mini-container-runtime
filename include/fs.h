#ifndef MCR_FS_H
#define MCR_FS_H

/* Stop mount events inside the container from leaking back to the host.
 * Must be called first thing inside the new mount namespace.
 * Returns 0 on success, -1 on error. */
int fs_make_mounts_private(void);

/* Switch the container's root to <rootfs> using pivot_root(2) and detach the
 * old root so the host filesystem is no longer reachable. Also sets up a
 * minimal /dev and a tmpfs /tmp inside the new root.
 * Returns 0 on success, -1 on error. */
int fs_setup_rootfs(const char *rootfs);

/* Check that <path> exists and is a directory. Returns 0 if ok, -1 if not. */
int fs_check_rootfs(const char *path);

/* Mount a fresh procfs on /proc so tools like ps only see the container's
 * own PID namespace. Returns 0 on success, -1 on error. */
int fs_mount_proc(void);

#endif /* MCR_FS_H */
