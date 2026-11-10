#define _GNU_SOURCE
#include "cgroup.h"
#include "util.h"
#include "test_util.h"

#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

/* The cgroup module is tested against a plain directory that pretends to be
 * the cgroup filesystem. This checks that we write the right values into the
 * right control files. Real enforcement is covered by tests/integration.sh. */

static char root[] = "/tmp/mcr-cgtest-XXXXXX";

static void read_value(const char *dir, const char *file, char *out, size_t len)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, file);
    if (read_file(path, out, len) < 0)
        out[0] = '\0';
}

static void test_create_and_limits(void)
{
    mcr_cgroup_t cg;
    char buf[128];
    struct stat st;

    CHECK(cgroup_create(&cg, "mcr-42") == 0);
    CHECK(cg.created == 1);
    CHECK(stat(cg.path, &st) == 0 && S_ISDIR(st.st_mode));

    char expect[300];
    snprintf(expect, sizeof(expect), "%s/mcr/mcr-42", root);
    CHECK_STR(cg.path, expect);

    /* controllers were handed down to the parent group */
    char parent[300];
    snprintf(parent, sizeof(parent), "%s/mcr", root);
    read_value(parent, "cgroup.subtree_control", buf, sizeof(buf));
    CHECK_STR(buf, "+pids");   /* last write wins in a fake file, "+memory" came first */

    CHECK(cgroup_set_memory(&cg, 64LL * 1024 * 1024) == 0);
    read_value(cg.path, "memory.max", buf, sizeof(buf));
    CHECK_STR(buf, "67108864");
    read_value(cg.path, "memory.swap.max", buf, sizeof(buf));
    CHECK_STR(buf, "0");
    read_value(cg.path, "memory.oom.group", buf, sizeof(buf));
    CHECK_STR(buf, "1");

    CHECK(cgroup_set_pids(&cg, 32) == 0);
    read_value(cg.path, "pids.max", buf, sizeof(buf));
    CHECK_STR(buf, "32");

    CHECK(cgroup_add_pid(&cg, 1234) == 0);
    read_value(cg.path, "cgroup.procs", buf, sizeof(buf));
    CHECK_STR(buf, "1234");

    CHECK(cgroup_destroy(&cg) == 0);
    CHECK(cg.created == 0);
    CHECK(stat(expect, &st) < 0);

    /* destroying twice is harmless */
    CHECK(cgroup_destroy(&cg) == 0);
}

static void test_oom_kills(void)
{
    mcr_cgroup_t cg;
    char path[512];

    CHECK(cgroup_create(&cg, "mcr-oom") == 0);

    /* no memory.events file yet */
    CHECK(cgroup_oom_kills(&cg) == -1);

    snprintf(path, sizeof(path), "%s/memory.events", cg.path);
    FILE *f = fopen(path, "w");
    CHECK(f != NULL);
    fputs("low 0\nhigh 0\nmax 12\noom 3\noom_kill 2\noom_group_kill 1\n", f);
    fclose(f);
    CHECK(cgroup_oom_kills(&cg) == 2);

    cgroup_destroy(&cg);
}

static void test_is_v2(void)
{
    /* a plain tmp directory is not a cgroup2 mount */
    CHECK(cgroup_is_v2() == 0);
}

static void test_create_failure(void)
{
    mcr_cgroup_t cg;
    cgroup_set_root("/nonexistent/cgroup/root");
    CHECK(cgroup_create(&cg, "mcr-bad") < 0);
    CHECK(cg.created == 0);
    cgroup_set_root(root);
}

int main(void)
{
    if (!mkdtemp(root)) {
        perror("mkdtemp");
        return 1;
    }
    cgroup_set_root(root);

    test_create_and_limits();
    test_oom_kills();
    test_is_v2();
    test_create_failure();

    char cmd[300];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", root);
    if (system(cmd) != 0)
        fprintf(stderr, "warning: could not remove %s\n", root);

    TEST_SUMMARY();
}
