#define _GNU_SOURCE
#include "util.h"
#include "test_util.h"

#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

static void test_write_and_read(const char *dir)
{
    char path[256];
    char buf[64];

    snprintf(path, sizeof(path), "%s/data.txt", dir);

    /* write_file opens an existing file, like cgroup control files */
    FILE *f = fopen(path, "w");
    CHECK(f != NULL);
    fclose(f);

    CHECK(write_file(path, "hello") == 0);
    CHECK(read_file(path, buf, sizeof(buf)) == 5);
    CHECK_STR(buf, "hello");

    /* a tiny buffer must truncate, never overflow */
    CHECK(read_file(path, buf, 3) == 2);
    CHECK_STR(buf, "he");

    CHECK(write_file("/nonexistent/dir/file", "x") < 0);
    CHECK(read_file("/nonexistent/dir/file", buf, sizeof(buf)) < 0);
    CHECK(read_file(path, buf, 0) < 0);
}

static void test_mkdir_p(const char *dir)
{
    char path[256];
    struct stat st;

    snprintf(path, sizeof(path), "%s/a/b/c", dir);
    CHECK(mkdir_p(path, 0755) == 0);
    CHECK(stat(path, &st) == 0 && S_ISDIR(st.st_mode));

    /* calling it again on an existing tree is fine */
    CHECK(mkdir_p(path, 0755) == 0);
    CHECK(mkdir_p("", 0755) < 0);
}

int main(void)
{
    char dir[] = "/tmp/mcr-test-XXXXXX";
    if (!mkdtemp(dir)) {
        perror("mkdtemp");
        return 1;
    }

    test_write_and_read(dir);
    test_mkdir_p(dir);

    char cmd[300];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", dir);
    if (system(cmd) != 0)
        fprintf(stderr, "warning: could not remove %s\n", dir);

    TEST_SUMMARY();
}
