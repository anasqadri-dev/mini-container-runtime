#define _GNU_SOURCE
#include "cli.h"

#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int parse_size(const char *text, long long *out)
{
    if (!text || !*text)
        return -1;

    /* reject a leading minus or plus explicitly, strtoll would accept them */
    if (!isdigit((unsigned char)text[0]))
        return -1;

    char *end = NULL;
    errno = 0;
    long long value = strtoll(text, &end, 10);
    if (errno != 0 || end == text || value < 0)
        return -1;

    long long mult = 1;
    if (*end != '\0') {
        if (end[1] != '\0')
            return -1; /* only one suffix character allowed */
        switch (tolower((unsigned char)*end)) {
        case 'k': mult = 1LL << 10; break;
        case 'm': mult = 1LL << 20; break;
        case 'g': mult = 1LL << 30; break;
        default:  return -1;
        }
    }

    if (value > LLONG_MAX / mult)
        return -1; /* overflow */

    *out = value * mult;
    return 0;
}

int parse_cpus(const char *text, long *out)
{
    if (!text || !isdigit((unsigned char)text[0]))
        return -1;

    long whole = 0, frac = 0;
    const char *p = text;

    while (isdigit((unsigned char)*p)) {
        whole = whole * 10 + (*p - '0');
        if (whole > 1024)
            return -1;
        p++;
    }

    if (*p == '.') {
        p++;
        int digits = 0;
        while (isdigit((unsigned char)*p)) {
            if (++digits > 3)
                return -1;            /* millicore precision is enough */
            frac = frac * 10 + (*p - '0');
            p++;
        }
        if (digits == 0)
            return -1;                /* "1." is not a number */
        for (; digits < 3; digits++)
            frac *= 10;
    }
    if (*p != '\0')
        return -1;

    long millis = whole * 1000 + frac;
    if (millis < 10 || millis > 1024 * 1000)
        return -1;                    /* kernel minimum quota is 1 ms per 100 ms period */

    *out = millis;
    return 0;
}

static void set_err(char *err, size_t errlen, const char *fmt, const char *arg)
{
    if (err && errlen)
        snprintf(err, errlen, fmt, arg ? arg : "");
}

static int parse_run(int argc, char **argv, mcr_options_t *opts, char *err, size_t errlen)
{
    static const struct option long_opts[] = {
        {"rootfs",   required_argument, NULL, 'r'},
        {"hostname", required_argument, NULL, 'n'},
        {"memory",   required_argument, NULL, 'm'},
        {"pids",     required_argument, NULL, 'p'},
        {"cpus",     required_argument, NULL, 'c'},
        {"verbose",  no_argument,       NULL, 'v'},
        {"detach",   no_argument,       NULL, 'd'},
        {"rm",       no_argument,       NULL, 'R'},
        {"help",     no_argument,       NULL, 'h'},
        {NULL, 0, NULL, 0}
    };

    /* The leading '+' makes getopt stop at the first non-option, so options
     * of the contained program are never eaten by us. */
    optind = 1;
    opterr = 0;

    int c;
    while ((c = getopt_long(argc, argv, "+r:n:m:p:c:vdh", long_opts, NULL)) != -1) {
        switch (c) {
        case 'r':
            opts->rootfs = optarg;
            break;
        case 'n':
            if (strlen(optarg) == 0 || strlen(optarg) > 64) {
                set_err(err, errlen, "invalid hostname '%s' (1-64 characters)", optarg);
                return -1;
            }
            opts->hostname = optarg;
            break;
        case 'm':
            if (parse_size(optarg, &opts->memory_bytes) < 0 || opts->memory_bytes == 0) {
                set_err(err, errlen, "invalid memory limit '%s' (try 64M or 1G)", optarg);
                return -1;
            }
            break;
        case 'p': {
            char *end = NULL;
            errno = 0;
            long n = strtol(optarg, &end, 10);
            if (errno != 0 || *end != '\0' || end == optarg || n < 1 || n > 4194304) {
                set_err(err, errlen, "invalid process limit '%s' (must be a positive number)", optarg);
                return -1;
            }
            opts->pids_max = n;
            break;
        }
        case 'c':
            if (parse_cpus(optarg, &opts->cpu_millicores) < 0) {
                set_err(err, errlen, "invalid cpu limit '%s' (try 0.5 or 2, range 0.01-1024)", optarg);
                return -1;
            }
            break;
        case 'v':
            opts->verbose = 1;
            break;
        case 'd':
            opts->detach = 1;
            break;
        case 'R':
            opts->auto_remove = 1;
            break;
        case 'h':
            opts->command = CMD_HELP;
            return 0;
        case '?':
            if (optopt)
                snprintf(err, errlen, "unknown or incomplete option '-%c'", optopt);
            else
                set_err(err, errlen, "unknown option '%s'", argv[optind - 1]);
            return -1;
        default:
            return -1;
        }
    }

    /* Allow an optional "--" separator before the command. */
    if (optind < argc && strcmp(argv[optind], "--") == 0)
        optind++;

    if (optind >= argc) {
        set_err(err, errlen, "%s", "no command given to run inside the container");
        return -1;
    }

    opts->cmd_argv = &argv[optind];
    opts->cmd_argc = argc - optind;
    return 0;
}

/* Shared by inspect, stop and rm: [options] <container-id>
 * short_opts/long_opts differ per command, the id is the only positional argument. */
static int parse_id_command(int argc, char **argv, mcr_options_t *opts,
                            const char *short_opts, const struct option *long_opts,
                            char *err, size_t errlen)
{
    optind = 1;
    opterr = 0;

    int c;
    while ((c = getopt_long(argc, argv, short_opts, long_opts, NULL)) != -1) {
        switch (c) {
        case 't': {
            char *end = NULL;
            errno = 0;
            long n = strtol(optarg, &end, 10);
            if (errno != 0 || *end != '\0' || end == optarg || n < 0 || n > 3600) {
                set_err(err, errlen, "invalid timeout '%s' (seconds, 0-3600)", optarg);
                return -1;
            }
            opts->stop_timeout = (int)n;
            break;
        }
        case 'f':
            opts->force = 1;
            break;
        case 'v':
            opts->verbose = 1;
            break;
        case 'h':
            opts->command = CMD_HELP;
            return 0;
        case '?':
            if (optopt)
                snprintf(err, errlen, "unknown or incomplete option '-%c'", optopt);
            else
                set_err(err, errlen, "unknown option '%s'", argv[optind - 1]);
            return -1;
        default:
            return -1;
        }
    }

    if (optind >= argc) {
        set_err(err, errlen, "%s", "missing container id");
        return -1;
    }
    if (argc - optind > 1) {
        set_err(err, errlen, "unexpected extra argument '%s'", argv[optind + 1]);
        return -1;
    }
    opts->id = argv[optind];
    return 0;
}

static int parse_ps(int argc, char **argv, mcr_options_t *opts, char *err, size_t errlen)
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            opts->command = CMD_HELP;
            return 0;
        }
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            opts->verbose = 1;
            continue;
        }
        set_err(err, errlen, "ps takes no arguments (got '%s')", argv[i]);
        return -1;
    }
    return 0;
}

int cli_parse(int argc, char **argv, mcr_options_t *opts, char *err, size_t errlen)
{
    memset(opts, 0, sizeof(*opts));
    opts->hostname = "container";

    if (argc < 2) {
        opts->command = CMD_HELP;
        return 0;
    }

    const char *sub = argv[1];

    if (strcmp(sub, "run") == 0) {
        opts->command = CMD_RUN;
        /* hand getopt a view that starts at the subcommand */
        return parse_run(argc - 1, argv + 1, opts, err, errlen);
    }
    if (strcmp(sub, "ps") == 0) {
        opts->command = CMD_PS;
        return parse_ps(argc - 1, argv + 1, opts, err, errlen);
    }
    if (strcmp(sub, "inspect") == 0) {
        static const struct option lo[] = {
            {"verbose", no_argument, NULL, 'v'}, {"help", no_argument, NULL, 'h'}, {NULL, 0, NULL, 0}};
        opts->command = CMD_INSPECT;
        return parse_id_command(argc - 1, argv + 1, opts, "+vh", lo, err, errlen);
    }
    if (strcmp(sub, "stop") == 0) {
        static const struct option lo[] = {
            {"time", required_argument, NULL, 't'}, {"verbose", no_argument, NULL, 'v'},
            {"help", no_argument, NULL, 'h'}, {NULL, 0, NULL, 0}};
        opts->command = CMD_STOP;
        opts->stop_timeout = MCR_DEFAULT_STOP_TIMEOUT;
        return parse_id_command(argc - 1, argv + 1, opts, "+t:vh", lo, err, errlen);
    }
    if (strcmp(sub, "rm") == 0) {
        static const struct option lo[] = {
            {"force", no_argument, NULL, 'f'}, {"time", required_argument, NULL, 't'},
            {"verbose", no_argument, NULL, 'v'}, {"help", no_argument, NULL, 'h'},
            {NULL, 0, NULL, 0}};
        opts->command = CMD_RM;
        opts->stop_timeout = MCR_DEFAULT_STOP_TIMEOUT;
        return parse_id_command(argc - 1, argv + 1, opts, "+ft:vh", lo, err, errlen);
    }
    if (strcmp(sub, "help") == 0 || strcmp(sub, "--help") == 0 || strcmp(sub, "-h") == 0) {
        opts->command = CMD_HELP;
        return 0;
    }
    if (strcmp(sub, "version") == 0 || strcmp(sub, "--version") == 0) {
        opts->command = CMD_VERSION;
        return 0;
    }

    set_err(err, errlen, "unknown command '%s'", sub);
    return -1;
}

void cli_print_usage(void)
{
    puts("Mini Container Runtime " MCR_VERSION "\n"
         "\n"
         "Usage:\n"
         "  mcr run [options] [--] <command> [args...]   start a container\n"
         "  mcr ps                                       list containers\n"
         "  mcr inspect <id>                             show details of a container\n"
         "  mcr stop [-t seconds] <id>                   stop a running container\n"
         "  mcr rm [-f] [-t seconds] <id>               remove a stopped container\n"
         "  mcr help | version\n"
         "\n"
         "Options for run:\n"
         "  -r, --rootfs <dir>     use <dir> as the container's root filesystem\n"
         "  -n, --hostname <name>  hostname inside the container (default: container)\n"
         "  -m, --memory <size>    memory limit, e.g. 64M or 1G (cgroups v2)\n"
         "  -p, --pids <n>         maximum number of processes (cgroups v2)\n"
         "  -c, --cpus <n>         CPU limit, e.g. 0.5 for half a CPU or 2 (cgroups v2)\n"
         "  -d, --detach           run in the background and print the container id\n"
         "      --rm               remove the container's record when it exits\n"
         "  -v, --verbose          print debug output\n"
         "  -h, --help             show this help\n"
         "\n"
         "Options for stop / rm:\n"
         "  -t, --time <seconds>   wait this long after SIGTERM before SIGKILL (default 10)\n"
         "  -f, --force            rm: stop the container first if it is still running\n"
         "\n"
         "An <id> may be shortened to any unique prefix.\n"
         "\n"
         "Examples:\n"
         "  sudo mcr run --hostname demo -- /bin/sh\n"
         "  sudo mcr run -d --rootfs ./rootfs --memory 64M --pids 32 --cpus 0.5 -- /bin/sleep 300\n"
         "  sudo mcr ps\n"
         "  sudo mcr stop a81f && sudo mcr rm a81f\n"
         "\n"
         "Running containers needs root (or CAP_SYS_ADMIN).");
}
