#ifndef MCR_COMMANDS_H
#define MCR_COMMANDS_H

#include "cli.h"

/* Lifecycle management commands. Each returns the process exit code:
 * 0 on success, 1 on error. */

int cmd_ps(const mcr_options_t *opts);       /* list containers                */
int cmd_inspect(const mcr_options_t *opts);  /* show details of one container  */
int cmd_stop(const mcr_options_t *opts);     /* SIGTERM, then SIGKILL          */
int cmd_rm(const mcr_options_t *opts);       /* delete a stopped container     */

#endif /* MCR_COMMANDS_H */
