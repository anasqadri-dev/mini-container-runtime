#define _GNU_SOURCE
#include "util.h"
#include "test_util.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
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

/* Write text to a new file and return its path in out. */
static void make_file(const char *dir, const char *name, const char *text, char *out, size_t len)
{
    snprintf(out, len, "%s/%s", dir, name);
    FILE *f = fopen(out, "w");
    if (f) {
        fputs(text, f);
        fclose(f);
    }
}

/* The text from the offset to the end of the file, as a string. */
static void text_from(const char *path, long lines, char *out, size_t len)
{
    out[0] = '\0';
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return;
    long off = file_tail_offset(fd, lines);
    if (off >= 0) {
        ssize_t n = pread(fd, out, len - 1, off);
        out[n < 0 ? 0 : n] = '\0';
    }
    close(fd);
}

static void test_tail_offset(const char *dir)
{
    char path[256], out[128];

    make_file(dir, "t1", "a\nb\nc\n", path, sizeof(path));
    text_from(path, 1, out, sizeof(out)); CHECK_STR(out, "c\n");
    text_from(path, 2, out, sizeof(out)); CHECK_STR(out, "b\nc\n");
    text_from(path, 3, out, sizeof(out)); CHECK_STR(out, "a\nb\nc\n");
    text_from(path, 10, out, sizeof(out)); CHECK_STR(out, "a\nb\nc\n");   /* fewer lines than asked */
    text_from(path, 0, out, sizeof(out)); CHECK_STR(out, "");              /* nothing wanted */

    /* a last line without a newline still counts as a line */
    make_file(dir, "t2", "a\nb\nc", path, sizeof(path));
    text_from(path, 1, out, sizeof(out)); CHECK_STR(out, "c");
    text_from(path, 2, out, sizeof(out)); CHECK_STR(out, "b\nc");

    /* blank lines are lines */
    make_file(dir, "t3", "a\n\n\nb\n", path, sizeof(path));
    text_from(path, 2, out, sizeof(out)); CHECK_STR(out, "\nb\n");
    text_from(path, 3, out, sizeof(out)); CHECK_STR(out, "\n\nb\n");

    /* single line, with and without newline */
    make_file(dir, "t4", "only\n", path, sizeof(path));
    text_from(path, 1, out, sizeof(out)); CHECK_STR(out, "only\n");
    make_file(dir, "t5", "only", path, sizeof(path));
    text_from(path, 1, out, sizeof(out)); CHECK_STR(out, "only");

    /* empty file */
    make_file(dir, "t6", "", path, sizeof(path));
    int fd = open(path, O_RDONLY);
    CHECK(file_tail_offset(fd, 5) == 0);
    CHECK(file_tail_offset(fd, 0) == 0);
    close(fd);

    /* a file much bigger than the internal 4 KiB buffer */
    snprintf(path, sizeof(path), "%s/big", dir);
    FILE *f = fopen(path, "w");
    for (int i = 1; i <= 5000; i++)
        fprintf(f, "line %d\n", i);
    fclose(f);
    text_from(path, 3, out, sizeof(out)); CHECK_STR(out, "line 4998\nline 4999\nline 5000\n");
    text_from(path, 1, out, sizeof(out)); CHECK_STR(out, "line 5000\n");

    fd = open(path, O_RDONLY);
    CHECK(file_tail_offset(fd, 5000) == 0);    /* exactly all lines */
    CHECK(file_tail_offset(fd, 99999) == 0);
    long off = file_tail_offset(fd, 4999);     /* everything except line 1 */
    CHECK(off == (long)strlen("line 1\n"));
    close(fd);

    CHECK(file_tail_offset(-1, 3) < 0);        /* bad descriptor */
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
    test_tail_offset(dir);

    char cmd[300];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", dir);
    if (system(cmd) != 0)
        fprintf(stderr, "warning: could not remove %s\n", dir);

    TEST_SUMMARY();
}
