#define _GNU_SOURCE
#include "state.h"
#include "test_util.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static char dir[] = "/tmp/mcr-statetest-XXXXXX";

static mcr_state_t make(const char *id, time_t created)
{
    mcr_state_t st;
    memset(&st, 0, sizeof(st));
    snprintf(st.id, sizeof(st.id), "%s", id);
    st.created = created;
    st.exit_code = -1;
    return st;
}

static void test_init_and_ids(void)
{
    mcr_state_t a, b;

    CHECK(state_init(&a) == 0);
    CHECK(strlen(a.id) == MCR_ID_LEN);
    for (const char *p = a.id; *p; p++)
        CHECK(isxdigit((unsigned char)*p) && !isupper((unsigned char)*p));
    CHECK(a.status == STATUS_CREATED);
    CHECK(a.exit_code == -1);
    CHECK(a.created > 0);

    CHECK(state_init(&b) == 0);
    CHECK(strcmp(a.id, b.id) != 0);
}

static void test_command_string(void)
{
    mcr_state_t st = make("00000001", 1);
    char *argv[] = {"/bin/echo", "hello", "multi\nline", NULL};

    state_set_command(&st, argv, 3);
    CHECK_STR(st.command, "/bin/echo hello multi line");

    /* a very long command is cut, not overflowed */
    char big[600];
    memset(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    char *argv2[] = {big, NULL};
    state_set_command(&st, argv2, 1);
    CHECK(strlen(st.command) < sizeof(st.command));
}

static void test_save_load_roundtrip(void)
{
    mcr_state_t st = make("a81f42c7", 1700000000);
    mcr_state_t back;

    st.status = STATUS_RUNNING;
    st.pid = 8231;
    st.runtime_pid = 8230;
    snprintf(st.command, sizeof(st.command), "/bin/sh -c echo a=b");   /* '=' in value */
    snprintf(st.hostname, sizeof(st.hostname), "boxed");
    snprintf(st.rootfs, sizeof(st.rootfs), "/srv/rootfs");
    st.memory_bytes = 64LL << 20;
    st.pids_max = 50;
    st.cpu_millicores = 1500;
    st.started = 1700000005;

    CHECK(state_save(&st) == 0);
    CHECK(state_load("a81f42c7", &back) == 0);

    CHECK_STR(back.id, "a81f42c7");
    CHECK(back.status == STATUS_RUNNING);
    CHECK(back.pid == 8231);
    CHECK(back.runtime_pid == 8230);
    CHECK(back.exit_code == -1);
    CHECK_STR(back.command, "/bin/sh -c echo a=b");
    CHECK_STR(back.hostname, "boxed");
    CHECK_STR(back.rootfs, "/srv/rootfs");
    CHECK(back.memory_bytes == 64LL << 20);
    CHECK(back.pids_max == 50);
    CHECK(back.cpu_millicores == 1500);
    CHECK(back.created == 1700000000);
    CHECK(back.started == 1700000005);

    /* saving again replaces the old record */
    st.status = STATUS_STOPPED;
    st.exit_code = 3;
    CHECK(state_save(&st) == 0);
    CHECK(state_load("a81f42c7", &back) == 0);
    CHECK(back.status == STATUS_STOPPED && back.exit_code == 3);

    /* no temp file is left behind */
    char tmp[600];
    state_path("a81f42c7", "state.tmp", tmp, sizeof(tmp));
    struct stat sb;
    CHECK(stat(tmp, &sb) < 0);
}

static void test_load_rejects_bad_ids(void)
{
    mcr_state_t st;

    CHECK(state_load("../../etc", &st) < 0);
    CHECK(state_load("short", &st) < 0);
    CHECK(state_load("ZZZZZZZZ", &st) < 0);
    CHECK(state_load("deadbeef", &st) < 0);   /* valid shape, does not exist */
    CHECK(state_remove("../../tmp") < 0);
}

static void test_find_by_prefix(void)
{
    mcr_state_t one = make("bb110001", 10), two = make("bb110002", 20), three = make("cc220003", 30);
    mcr_state_t out;

    CHECK(state_save(&one) == 0);
    CHECK(state_save(&two) == 0);
    CHECK(state_save(&three) == 0);

    CHECK(state_find("cc", &out) == 0 && strcmp(out.id, "cc220003") == 0);
    CHECK(state_find("bb11000", &out) == -2);            /* matches both */
    CHECK(state_find("bb110002", &out) == 0 && strcmp(out.id, "bb110002") == 0);
    CHECK(state_find("bb1100", &out) == -2);
    CHECK(state_find("dd", &out) == -1);
    CHECK(state_find("", &out) == -1);
    CHECK(state_find("123456789", &out) == -1);          /* longer than an id */
}

static void test_list_and_remove(void)
{
    mcr_state_t *list = NULL;
    size_t n = 0;

    CHECK(state_list(&list, &n) == 0);
    CHECK(n == 4);   /* a81f42c7 and the three from test_find_by_prefix */

    /* oldest first */
    int sorted = 1;
    for (size_t i = 1; i < n; i++)
        if (list[i - 1].created > list[i].created)
            sorted = 0;
    CHECK(sorted);
    free(list);

    /* stray files and directories with other names are ignored */
    char junk[600];
    snprintf(junk, sizeof(junk), "%s/not-a-container", dir);
    mkdir(junk, 0755);
    CHECK(state_list(&list, &n) == 0 && n == 4);
    free(list);

    /* removing also deletes extra files such as the log */
    char log[600];
    state_path("cc220003", "output.log", log, sizeof(log));
    FILE *f = fopen(log, "w");
    CHECK(f != NULL);
    if (f) {
        fputs("hi\n", f);
        fclose(f);
    }
    CHECK(state_remove("cc220003") == 0);

    mcr_state_t out;
    CHECK(state_find("cc220003", &out) == -1);
    CHECK(state_list(&list, &n) == 0 && n == 3);
    free(list);
    CHECK(state_remove("cc220003") < 0);   /* already gone */
}

static void test_refresh(void)
{
    mcr_state_t st = make("dd330001", 5);

    /* a child that has exited and been reaped gives us a pid that is dead */
    pid_t dead = fork();
    if (dead == 0)
        _exit(0);
    waitpid(dead, NULL, 0);

    /* runtime still running: nothing changes */
    st.status = STATUS_RUNNING;
    st.runtime_pid = getpid();
    st.pid = dead;
    CHECK(state_refresh(&st) == 0 && st.status == STATUS_RUNNING);

    /* only the container is alive (supervisor died): still considered running */
    st.runtime_pid = dead;
    st.pid = getpid();
    CHECK(state_refresh(&st) == 0 && st.status == STATUS_RUNNING);

    /* both are gone: stale record becomes STOPPED with unknown exit code */
    st.runtime_pid = dead;
    st.pid = dead;
    CHECK(state_refresh(&st) == 1);
    CHECK(st.status == STATUS_STOPPED);
    CHECK(st.exit_code == -1);
    CHECK(st.finished > 0);

    /* already stopped: untouched */
    CHECK(state_refresh(&st) == 0);

    /* a record without pids is still being set up, not stale */
    mcr_state_t fresh = make("dd330002", 6);
    CHECK(state_refresh(&fresh) == 0 && fresh.status == STATUS_CREATED);
}

static void test_status_names(void)
{
    CHECK_STR(status_name(STATUS_CREATED), "CREATED");
    CHECK_STR(status_name(STATUS_RUNNING), "RUNNING");
    CHECK_STR(status_name(STATUS_STOPPED), "STOPPED");
}

int main(void)
{
    if (!mkdtemp(dir)) {
        perror("mkdtemp");
        return 1;
    }
    setenv("MCR_STATE_DIR", dir, 1);
    CHECK_STR(state_dir(), dir);

    /* before anything is saved the directory is empty */
    mcr_state_t *list = NULL;
    size_t n = 99;
    CHECK(state_list(&list, &n) == 0 && n == 0);
    free(list);

    test_status_names();
    test_init_and_ids();
    test_command_string();
    test_save_load_roundtrip();
    test_load_rejects_bad_ids();
    test_find_by_prefix();
    test_list_and_remove();
    test_refresh();

    char cmd[300];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", dir);
    if (system(cmd) != 0)
        fprintf(stderr, "warning: could not remove %s\n", dir);

    TEST_SUMMARY();
}
