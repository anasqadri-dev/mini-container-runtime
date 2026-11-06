#include "cli.h"
#include "test_util.h"

static void test_parse_size(void)
{
    long long v = 0;

    CHECK(parse_size("0", &v) == 0 && v == 0);
    CHECK(parse_size("1048576", &v) == 0 && v == 1048576);
    CHECK(parse_size("512k", &v) == 0 && v == 512LL * 1024);
    CHECK(parse_size("512K", &v) == 0 && v == 512LL * 1024);
    CHECK(parse_size("64M", &v) == 0 && v == 64LL * 1024 * 1024);
    CHECK(parse_size("2g", &v) == 0 && v == 2LL * 1024 * 1024 * 1024);

    /* bad input */
    CHECK(parse_size(NULL, &v) < 0);
    CHECK(parse_size("", &v) < 0);
    CHECK(parse_size("abc", &v) < 0);
    CHECK(parse_size("-5", &v) < 0);
    CHECK(parse_size("+5", &v) < 0);
    CHECK(parse_size("10x", &v) < 0);
    CHECK(parse_size("10MB", &v) < 0);
    CHECK(parse_size("1.5G", &v) < 0);
    CHECK(parse_size("99999999999999999999", &v) < 0);
    CHECK(parse_size("9223372036854775807G", &v) < 0); /* overflow after suffix */
}

static void test_no_args_shows_help(void)
{
    mcr_options_t o;
    char err[128];
    char *argv[] = {"mcr", NULL};

    CHECK(cli_parse(1, argv, &o, err, sizeof(err)) == 0);
    CHECK(o.command == CMD_HELP);
}

static void test_version_and_help(void)
{
    mcr_options_t o;
    char err[128];

    char *v[] = {"mcr", "version", NULL};
    CHECK(cli_parse(2, v, &o, err, sizeof(err)) == 0 && o.command == CMD_VERSION);

    char *h[] = {"mcr", "--help", NULL};
    CHECK(cli_parse(2, h, &o, err, sizeof(err)) == 0 && o.command == CMD_HELP);
}

static void test_run_defaults(void)
{
    mcr_options_t o;
    char err[128];
    char *argv[] = {"mcr", "run", "/bin/sh", NULL};

    CHECK(cli_parse(3, argv, &o, err, sizeof(err)) == 0);
    CHECK(o.command == CMD_RUN);
    CHECK(o.rootfs == NULL);
    CHECK_STR(o.hostname, "container");
    CHECK(o.memory_bytes == 0);
    CHECK(o.pids_max == 0);
    CHECK(o.cmd_argc == 1);
    CHECK_STR(o.cmd_argv[0], "/bin/sh");
}

static void test_run_all_options(void)
{
    mcr_options_t o;
    char err[128];
    char *argv[] = {"mcr", "run", "--rootfs", "/tmp/rfs", "-n", "demo",
                    "--memory", "64M", "-p", "20", "-v", "--",
                    "/bin/echo", "-n", "hi", NULL};

    CHECK(cli_parse(15, argv, &o, err, sizeof(err)) == 0);
    CHECK_STR(o.rootfs, "/tmp/rfs");
    CHECK_STR(o.hostname, "demo");
    CHECK(o.memory_bytes == 64LL * 1024 * 1024);
    CHECK(o.pids_max == 20);
    CHECK(o.verbose == 1);
    CHECK(o.cmd_argc == 3);
    CHECK_STR(o.cmd_argv[0], "/bin/echo");
    CHECK_STR(o.cmd_argv[1], "-n");
    CHECK_STR(o.cmd_argv[2], "hi");
}

static void test_options_after_command_belong_to_command(void)
{
    mcr_options_t o;
    char err[128];
    char *argv[] = {"mcr", "run", "ls", "-la", "--verbose", NULL};

    CHECK(cli_parse(5, argv, &o, err, sizeof(err)) == 0);
    CHECK(o.verbose == 0);
    CHECK(o.cmd_argc == 3);
    CHECK_STR(o.cmd_argv[1], "-la");
}

static void test_errors(void)
{
    mcr_options_t o;
    char err[128];

    char *unknown[] = {"mcr", "frobnicate", NULL};
    CHECK(cli_parse(2, unknown, &o, err, sizeof(err)) < 0);

    char *nocmd[] = {"mcr", "run", "--memory", "64M", NULL};
    CHECK(cli_parse(4, nocmd, &o, err, sizeof(err)) < 0);

    char *badmem[] = {"mcr", "run", "-m", "lots", "ls", NULL};
    CHECK(cli_parse(5, badmem, &o, err, sizeof(err)) < 0);

    char *zeromem[] = {"mcr", "run", "-m", "0", "ls", NULL};
    CHECK(cli_parse(5, zeromem, &o, err, sizeof(err)) < 0);

    char *badpids[] = {"mcr", "run", "-p", "0", "ls", NULL};
    CHECK(cli_parse(5, badpids, &o, err, sizeof(err)) < 0);

    char *badpids2[] = {"mcr", "run", "-p", "ten", "ls", NULL};
    CHECK(cli_parse(5, badpids2, &o, err, sizeof(err)) < 0);

    char *badopt[] = {"mcr", "run", "--nope", "ls", NULL};
    CHECK(cli_parse(4, badopt, &o, err, sizeof(err)) < 0);

    char *nohost[] = {"mcr", "run", "-n", "", "ls", NULL};
    CHECK(cli_parse(5, nohost, &o, err, sizeof(err)) < 0);
}

int main(void)
{
    test_parse_size();
    test_no_args_shows_help();
    test_version_and_help();
    test_run_defaults();
    test_run_all_options();
    test_options_after_command_belong_to_command();
    test_errors();
    TEST_SUMMARY();
}
