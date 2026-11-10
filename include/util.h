#ifndef MCR_UTIL_H
#define MCR_UTIL_H

#include <stddef.h>

/* Logging ------------------------------------------------------------- */

extern int mcr_verbose;

void mcr_log(const char *level, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

#define LOG_INFO(...)  mcr_log("info", __VA_ARGS__)
#define LOG_WARN(...)  mcr_log("warn", __VA_ARGS__)
#define LOG_ERR(...)   mcr_log("error", __VA_ARGS__)
#define LOG_DEBUG(...) do { if (mcr_verbose) mcr_log("debug", __VA_ARGS__); } while (0)

/* File helpers -------------------------------------------------------- */

/* Write a string to an existing file (used for cgroup control files).
 * Returns 0 on success, -1 on error (errno is set). */
int write_file(const char *path, const char *data);

/* Read a whole (small) file into buf, NUL terminated.
 * Returns number of bytes read, or -1 on error. */
int read_file(const char *path, char *buf, size_t buflen);

/* mkdir -p. Returns 0 on success, -1 on error. */
int mkdir_p(const char *path, unsigned int mode);

#endif /* MCR_UTIL_H */
