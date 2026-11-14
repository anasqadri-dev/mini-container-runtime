#define _GNU_SOURCE
#include "util.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int mcr_verbose = 0;

void mcr_log(const char *level, const char *fmt, ...)
{
    va_list ap;

    fprintf(stderr, "[mcr %s] ", level);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

int write_file(const char *path, const char *data)
{
    int fd = open(path, O_WRONLY);
    if (fd < 0)
        return -1;

    size_t len = strlen(data);
    ssize_t n = write(fd, data, len);
    int saved = errno;
    close(fd);

    if (n < 0 || (size_t)n != len) {
        errno = (n < 0) ? saved : EIO;
        return -1;
    }
    return 0;
}

int read_file(const char *path, char *buf, size_t buflen)
{
    if (buflen == 0)
        return -1;

    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;

    ssize_t n = read(fd, buf, buflen - 1);
    int saved = errno;
    close(fd);

    if (n < 0) {
        errno = saved;
        return -1;
    }
    buf[n] = '\0';
    return (int)n;
}

int mkdir_p(const char *path, unsigned int mode)
{
    char tmp[4096];
    size_t len = strlen(path);

    if (len == 0 || len >= sizeof(tmp)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(tmp, path, len + 1);

    for (char *p = tmp + 1; *p; p++) {
        if (*p != '/')
            continue;
        *p = '\0';
        if (mkdir(tmp, mode) < 0 && errno != EEXIST)
            return -1;
        *p = '/';
    }
    if (mkdir(tmp, mode) < 0 && errno != EEXIST)
        return -1;
    return 0;
}

long file_tail_offset(int fd, long lines)
{
    off_t size = lseek(fd, 0, SEEK_END);
    if (size < 0)
        return -1;
    if (lines <= 0 || size == 0)
        return (long)size;

    char buf[4096];
    off_t pos = size;
    long seen = 0;

    while (pos > 0) {
        size_t chunk = pos > (off_t)sizeof(buf) ? sizeof(buf) : (size_t)pos;
        pos -= (off_t)chunk;

        size_t got = 0;
        while (got < chunk) {
            ssize_t n = pread(fd, buf + got, chunk - got, pos + (off_t)got);
            if (n < 0) {
                if (errno == EINTR)
                    continue;
                return -1;
            }
            if (n == 0)
                return -1;   /* the file shrank while we were reading it */
            got += (size_t)n;
        }

        for (size_t i = chunk; i > 0; i--) {
            off_t idx = pos + (off_t)i - 1;

            if (buf[i - 1] != '\n')
                continue;
            if (idx == size - 1)
                continue;    /* the newline that ends the last line is not a separator */
            if (++seen == lines)
                return (long)(idx + 1);
        }
    }
    return 0;
}
