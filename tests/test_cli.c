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

static void test_run_detach_and_rm(void)
{
    mcr_options_t o;
    char err[128];
    char *argv[] = {"mcr", "run", "-d", "--rm", "/bin/sleep", "60", NULL};

    CHECK(cli_parse(6, argv, &o, err, sizeof(err)) == 0);
    CHECK(o.command == CMD_RUN);
    CHECK(o.detach == 1);
    CHECK(o.auto_remove == 1);
    CHECK(o.cmd_argc == 2);

    char *plain[] = {"mcr", "run", "ls", NULL};
    CHECK(cli_parse(3, plain, &o, err, sizeof(err)) == 0);
    CHECK(o.detach == 0 && o.auto_remove == 0);
}

static void test_ps(void)
{
    mcr_options_t o;
    char err[128];

    char *ok[] = {"mcr", "ps", NULL};
    CHECK(cli_parse(2, ok, &o, err, sizeof(err)) == 0 && o.command == CMD_PS);

    char *extra[] = {"mcr", "ps", "something", NULL};
    CHECK(cli_parse(3, extra, &o, err, sizeof(err)) < 0);
}

static void test_inspect(void)
{
    mcr_options_t o;
    char err[128];

    char *ok[] = {"mcr", "inspect", "a81f42c7", NULL};
    CHECK(cli_parse(3, ok, &o, err, sizeof(err)) == 0);
    CHECK(o.command == CMD_INSPECT);
    CHECK_STR(o.id, "a81f42c7");

    char *none[] = {"mcr", "inspect", NULL};
    CHECK(cli_parse(2, none, &o, err, sizeof(err)) < 0);

    char *two[] = {"mcr", "inspect", "a", "b", NULL};
    CHECK(cli_parse(4, two, &o, err, sizeof(err)) < 0);
}

static void test_stop(void)
{
    mcr_options_t o;
    char err[128];

    char *dflt[] = {"mcr", "stop", "a81f", NULL};
    CHECK(cli_parse(3, dflt, &o, err, sizeof(err)) == 0);
    CHECK(o.command == CMD_STOP);
    CHECK_STR(o.id, "a81f");
    CHECK(o.stop_timeout == MCR_DEFAULT_STOP_TIMEOUT);

    char *timed[] = {"mcr", "stop", "-t", "3", "a81f", NULL};
    CHECK(cli_parse(5, timed, &o, err, sizeof(err)) == 0 && o.stop_timeout == 3);

    char *zero[] = {"mcr", "stop", "--time", "0", "a81f", NULL};
    CHECK(cli_parse(5, zero, &o, err, sizeof(err)) == 0 && o.stop_timeout == 0);

    char *bad[] = {"mcr", "stop", "-t", "soon", "a81f", NULL};
    CHECK(cli_parse(5, bad, &o, err, sizeof(err)) < 0);

    char *neg[] = {"mcr", "stop", "-t", "-1", "a81f", NULL};
    CHECK(cli_parse(5, neg, &o, err, sizeof(err)) < 0);

    char *noid[] = {"mcr", "stop", "-t", "3", NULL};
    CHECK(cli_parse(4, noid, &o, err, sizeof(err)) < 0);
}

static void test_rm(void)
{
    mcr_options_t o;
    char err[128];

    char *plain[] = {"mcr", "rm", "a81f", NULL};
    CHECK(cli_parse(3, plain, &o, err, sizeof(err)) == 0);
    CHECK(o.command == CMD_RM && o.force == 0);

    char *force[] = {"mcr", "rm", "-f", "a81f", NULL};
    CHECK(cli_parse(4, force, &o, err, sizeof(err)) == 0 && o.force == 1);

    char *longf[] = {"mcr", "rm", "--force", "a81f", NULL};
    CHECK(cli_parse(4, longf, &o, err, sizeof(err)) == 0 && o.force == 1);

    char *timed[] = {"mcr", "rm", "-f", "-t", "2", "a81f", NULL};
    CHECK(cli_parse(6, timed, &o, err, sizeof(err)) == 0);
    CHECK(o.force == 1 && o.stop_timeout == 2);

    char *unknown[] = {"mcr", "rm", "-x", "a81f", NULL};
    CHECK(cli_parse(4, unknown, &o, err, sizeof(err)) < 0);
}

static void test_parse_cpus(void)
{
    long v = 0;

    CHECK(parse_cpus("1", &v) == 0 && v == 1000);
    CHECK(parse_cpus("2", &v) == 0 && v == 2000);
    CHECK(parse_cpus("0.5", &v) == 0 && v == 500);
    CHECK(parse_cpus("0.25", &v) == 0 && v == 250);
    CHECK(parse_cpus("1.5", &v) == 0 && v == 1500);
    CHECK(parse_cpus("1.125", &v) == 0 && v == 1125);
    CHECK(parse_cpus("0.01", &v) == 0 && v == 10);       /* smallest allowed */
    CHECK(parse_cpus("1024", &v) == 0 && v == 1024000);  /* largest allowed */
    CHECK(parse_cpus("00.5", &v) == 0 && v == 500);

    /* bad input */
    CHECK(parse_cpus(NULL, &v) < 0);
    CHECK(parse_cpus("", &v) < 0);
    CHECK(parse_cpus("0", &v) < 0);
    CHECK(parse_cpus("0.0", &v) < 0);
    CHECK(parse_cpus("0.009", &v) < 0);     /* below the kernel's minimum quota */
    CHECK(parse_cpus("0.001", &v) < 0);
    CHECK(parse_cpus("1025", &v) < 0);
    CHECK(parse_cpus("99999999999999999999", &v) < 0);
    CHECK(parse_cpus("-1", &v) < 0);
    CHECK(parse_cpus("+1", &v) < 0);
    CHECK(parse_cpus(".5", &v) < 0);
    CHECK(parse_cpus("1.", &v) < 0);
    CHECK(parse_cpus("1.2345", &v) < 0);    /* more than three decimals */
    CHECK(parse_cpus("1e3", &v) < 0);
    CHECK(parse_cpus("2cpus", &v) < 0);
    CHECK(parse_cpus("1,5", &v) < 0);
}

static void test_run_cpus_option(void)
{
    mcr_options_t o;
    char err[128];

    char *argv[] = {"mcr", "run", "--cpus", "0.5", "-m", "64M", "/bin/sh", NULL};
    CHECK(cli_parse(7, argv, &o, err, sizeof(err)) == 0);
    CHECK(o.cpu_millicores == 500);
    CHECK(o.memory_bytes == 64LL * 1024 * 1024);

    char *shortf[] = {"mcr", "run", "-c", "2", "/bin/sh", NULL};
    CHECK(cli_parse(5, shortf, &o, err, sizeof(err)) == 0 && o.cpu_millicores == 2000);

    char *none[] = {"mcr", "run", "/bin/sh", NULL};
    CHECK(cli_parse(3, none, &o, err, sizeof(err)) == 0 && o.cpu_millicores == 0);

    char *bad[] = {"mcr", "run", "--cpus", "lots", "/bin/sh", NULL};
    CHECK(cli_parse(5, bad, &o, err, sizeof(err)) < 0);

    char *zero[] = {"mcr", "run", "--cpus", "0", "/bin/sh", NULL};
    CHECK(cli_parse(5, zero, &o, err, sizeof(err)) < 0);

    /* an option after the command belongs to the command, not to mcr */
    char *after[] = {"mcr", "run", "/bin/sh", "--cpus", "9", NULL};
    CHECK(cli_parse(5, after, &o, err, sizeof(err)) == 0);
    CHECK(o.cpu_millicores == 0 && o.cmd_argc == 3);
}

static void test_logs(void)
{
    mcr_options_t o;
    char err[128];

    char *plain[] = {"mcr", "logs", "a81f", NULL};
    CHECK(cli_parse(3, plain, &o, err, sizeof(err)) == 0);
    CHECK(o.command == CMD_LOGS);
    CHECK_STR(o.id, "a81f");
    CHECK(o.follow == 0);
    CHECK(o.tail_lines == -1);      /* default: everything */
    CHECK(o.force == 0);

    char *follow[] = {"mcr", "logs", "-f", "a81f", NULL};
    CHECK(cli_parse(4, follow, &o, err, sizeof(err)) == 0);
    CHECK(o.follow == 1 && o.force == 0);   /* -f means follow here, not force */

    char *longf[] = {"mcr", "logs", "--follow", "--tail", "20", "a81f", NULL};
    CHECK(cli_parse(6, longf, &o, err, sizeof(err)) == 0);
    CHECK(o.follow == 1 && o.tail_lines == 20);

    char *shortn[] = {"mcr", "logs", "-n", "0", "a81f", NULL};
    CHECK(cli_parse(5, shortn, &o, err, sizeof(err)) == 0 && o.tail_lines == 0);

    char *badn[] = {"mcr", "logs", "-n", "many", "a81f", NULL};
    CHECK(cli_parse(5, badn, &o, err, sizeof(err)) < 0);

    char *negn[] = {"mcr", "logs", "-n", "-3", "a81f", NULL};
    CHECK(cli_parse(5, negn, &o, err, sizeof(err)) < 0);

    char *noid[] = {"mcr", "logs", "-f", NULL};
    CHECK(cli_parse(3, noid, &o, err, sizeof(err)) < 0);

    char *extra[] = {"mcr", "logs", "a81f", "b2", NULL};
    CHECK(cli_parse(4, extra, &o, err, sizeof(err)) < 0);

    /* -f still means force for rm */
    char *rm[] = {"mcr", "rm", "-f", "a81f", NULL};
    CHECK(cli_parse(4, rm, &o, err, sizeof(err)) == 0 && o.force == 1 && o.follow == 0);

    /* -n is not an option of stop or inspect */
    char *stop[] = {"mcr", "stop", "-n", "3", "a81f", NULL};
    CHECK(cli_parse(5, stop, &o, err, sizeof(err)) < 0);
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
    test_run_detach_and_rm();
    test_ps();
    test_inspect();
    test_stop();
    test_rm();
    test_parse_cpus();
    test_run_cpus_option();
    test_logs();
    TEST_SUMMARY();
}
