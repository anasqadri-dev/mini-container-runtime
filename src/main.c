#include <stdio.h>

#include "cli.h"
#include "commands.h"
#include "container.h"
#include "util.h"

int main(int argc, char **argv)
{
    mcr_options_t opts;
    char err[256] = "";

    if (cli_parse(argc, argv, &opts, err, sizeof(err)) < 0) {
        fprintf(stderr, "mcr: %s\n\n", err);
        cli_print_usage();
        return 2;
    }

    mcr_verbose = opts.verbose;

    switch (opts.command) {
    case CMD_VERSION:
        printf("mcr %s\n", MCR_VERSION);
        return 0;
    case CMD_RUN:
        return container_run(&opts);
    case CMD_PS:
        return cmd_ps(&opts);
    case CMD_INSPECT:
        return cmd_inspect(&opts);
    case CMD_STOP:
        return cmd_stop(&opts);
    case CMD_RM:
        return cmd_rm(&opts);
    case CMD_HELP:
    default:
        cli_print_usage();
        return 0;
    }
}
