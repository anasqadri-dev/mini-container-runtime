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
        {"verbose",  no_argument,       NULL, 'v'},
        {"help",     no_argument,       NULL, 'h'},
        {NULL, 0, NULL, 0}
    };

    /* The leading '+' makes getopt stop at the first non-option, so options
     * of the contained program are never eaten by us. */
    optind = 1;
    opterr = 0;

    int c;
    while ((c = getopt_long(argc, argv, "+r:n:m:p:vh", long_opts, NULL)) != -1) {
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
         "  mcr run [options] [--] <command> [args...]\n"
         "  mcr help\n"
         "  mcr version\n"
         "\n"
         "Options for run:\n"
         "  -r, --rootfs <dir>     use <dir> as the container's root filesystem\n"
         "  -n, --hostname <name>  hostname inside the container (default: container)\n"
         "  -m, --memory <size>    memory limit, e.g. 64M or 1G (cgroups v2)\n"
         "  -p, --pids <n>         maximum number of processes (cgroups v2)\n"
         "  -v, --verbose          print debug output\n"
         "  -h, --help             show this help\n"
         "\n"
         "Examples:\n"
         "  sudo mcr run --hostname demo -- /bin/sh\n"
         "  sudo mcr run --rootfs ./rootfs --memory 64M --pids 32 -- /bin/sh\n"
         "\n"
         "Running a container needs root (or CAP_SYS_ADMIN).");
}
