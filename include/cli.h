#ifndef MCR_CLI_H
#define MCR_CLI_H

#include <stddef.h>

#define MCR_VERSION "0.2.0"
#define MCR_DEFAULT_STOP_TIMEOUT 10

typedef enum {
    CMD_NONE = 0,
    CMD_RUN,
    CMD_PS,
    CMD_INSPECT,
    CMD_STOP,
    CMD_RM,
    CMD_HELP,
    CMD_VERSION
} mcr_command_t;

typedef struct {
    mcr_command_t command;

    /* options for "run" */
    const char *rootfs;       /* NULL = keep host filesystem */
    const char *hostname;     /* hostname inside the container */
    long long   memory_bytes; /* 0 = unlimited */
    long        pids_max;     /* 0 = unlimited */
    long        cpu_millicores; /* 1000 = one full CPU, 0 = unlimited */
    int         verbose;
    int         detach;       /* run: start in the background and print the id */
    int         auto_remove;  /* run: delete the container's metadata on exit */

    /* inspect / stop / rm */
    const char *id;           /* container id or unique prefix */
    int         stop_timeout; /* stop: seconds to wait after SIGTERM before SIGKILL */
    int         force;        /* rm: stop a running container first */

    char      **cmd_argv;     /* program to run inside the container */
    int         cmd_argc;
} mcr_options_t;

/* Parse a human friendly size such as "64M", "512k", "1G" or "1048576".
 * Suffixes are powers of 1024 and case-insensitive.
 * Returns 0 on success and stores the byte count in *out, -1 on bad input. */
int parse_size(const char *text, long long *out);

/* Parse a CPU amount such as "0.5", "2" or "1.25" into millicores
 * (1000 = one CPU). At most three decimals; the range is 0.01 to 1024.
 * Returns 0 on success and stores the value in *out, -1 on bad input. */
int parse_cpus(const char *text, long *out);

/* Parse command line arguments.
 * On failure returns -1 and writes a short message into err. */
int cli_parse(int argc, char **argv, mcr_options_t *opts, char *err, size_t errlen);

void cli_print_usage(void);

#endif /* MCR_CLI_H */
