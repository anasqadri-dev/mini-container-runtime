#ifndef MCR_CONTAINER_H
#define MCR_CONTAINER_H

#include "cli.h"

/* Run the command described by opts inside a new container and wait for it.
 * Returns the exit status of the contained program (128+N if it was killed by
 * signal N), or a value >= 125 when the runtime itself failed. */
int container_run(const mcr_options_t *opts);

#endif /* MCR_CONTAINER_H */
