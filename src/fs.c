#define _GNU_SOURCE
#include "fs.h"
#include "util.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/syscall.h>
#include <unistd.h>

int fs_make_mounts_private(void)
{
    /* systemd marks / as "shared", so without this our mounts (and later our
     * unmounts) would propagate to the host's mount table. */
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) < 0) {
        LOG_ERR("cannot make mounts private: %s", strerror(errno));
        return -1;
    }
    return 0;
}

int fs_check_rootfs(const char *path)
{
    struct stat st;

    if (stat(path, &st) < 0) {
        LOG_ERR("rootfs '%s': %s", path, strerror(errno));
        return -1;
    }
    if (!S_ISDIR(st.st_mode)) {
        LOG_ERR("rootfs '%s' is not a directory", path);
        return -1;
    }
    return 0;
}

/* Create the few device nodes almost every program expects. */
static int setup_dev(void)
{
    static const struct {
        const char *name;
        unsigned int major, minor;
    } devs[] = {
        {"null", 1, 3}, {"zero", 1, 5}, {"random", 1, 8},
        {"urandom", 1, 9}, {"tty", 5, 0},
    };

    if (mkdir_p("/dev", 0755) < 0)
        return -1;
    if (mount("tmpfs", "/dev", "tmpfs", MS_NOSUID | MS_STRICTATIME, "mode=755,size=1m") < 0) {
        LOG_ERR("cannot mount /dev: %s", strerror(errno));
        return -1;
    }

    for (size_t i = 0; i < sizeof(devs) / sizeof(devs[0]); i++) {
        char path[64];
        snprintf(path, sizeof(path), "/dev/%s", devs[i].name);
        if (mknod(path, S_IFCHR | 0666, makedev(devs[i].major, devs[i].minor)) < 0) {
            LOG_ERR("cannot create %s: %s", path, strerror(errno));
            return -1;
        }
    }
    return 0;
}

int fs_setup_rootfs(const char *rootfs)
{
    /* pivot_root needs the new root to be a mount point, so bind it onto itself */
    if (mount(rootfs, rootfs, NULL, MS_BIND | MS_REC, NULL) < 0) {
        LOG_ERR("cannot bind mount rootfs '%s': %s", rootfs, strerror(errno));
        return -1;
    }

    if (chdir(rootfs) < 0) {
        LOG_ERR("cannot chdir to rootfs '%s': %s", rootfs, strerror(errno));
        return -1;
    }

    /* Park the old root in a directory inside the new one, then drop it. */
    if (mkdir(".old_root", 0700) < 0 && errno != EEXIST) {
        LOG_ERR("cannot create .old_root in rootfs: %s", strerror(errno));
        return -1;
    }

    if (syscall(SYS_pivot_root, ".", ".old_root") < 0) {
        LOG_ERR("pivot_root failed: %s", strerror(errno));
        return -1;
    }

    if (chdir("/") < 0) {
        LOG_ERR("chdir to new root failed: %s", strerror(errno));
        return -1;
    }

    if (umount2("/.old_root", MNT_DETACH) < 0) {
        LOG_ERR("cannot unmount old root: %s", strerror(errno));
        return -1;
    }
    rmdir("/.old_root");

    if (setup_dev() < 0)
        return -1;

    if (mkdir_p("/tmp", 01777) == 0)
        mount("tmpfs", "/tmp", "tmpfs", MS_NOSUID | MS_NODEV, "mode=1777,size=16m");

    LOG_DEBUG("pivoted into rootfs %s", rootfs);
    return 0;
}

int fs_mount_proc(void)
{
    if (mkdir_p("/proc", 0555) < 0) {
        LOG_ERR("cannot create /proc: %s", strerror(errno));
        return -1;
    }

    if (mount("proc", "/proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) < 0) {
        LOG_ERR("cannot mount /proc: %s", strerror(errno));
        return -1;
    }
    LOG_DEBUG("mounted procfs on /proc");
    return 0;
}
