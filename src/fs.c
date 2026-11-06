#define _GNU_SOURCE
#include "fs.h"
#include "util.h"

#include <errno.h>
#include <string.h>
#include <sys/mount.h>

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
