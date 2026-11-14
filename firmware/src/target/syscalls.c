/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Martin Faltus (mafaltu) */
/* Minimal newlib syscalls. The core uses snprintf/vsnprintf only (no files, no console). newlib-nano's stdio
 * object still references these four; the -specs=nosys.specs versions carry a link-time "not implemented"
 * warning. These replace them with the same behaviour (fail with ENOSYS) and no warning. Never called. */
#include <errno.h>
#include <sys/types.h>

int _close(int fd);
off_t _lseek(int fd, off_t off, int whence);
int _read(int fd, char *buf, int len);
int _write(int fd, const char *buf, int len);

int _close(int fd)
{
    (void)fd;
    errno = ENOSYS;
    return -1;
}

off_t _lseek(int fd, off_t off, int whence)
{
    (void)fd;
    (void)off;
    (void)whence;
    errno = ENOSYS;
    return -1;
}

int _read(int fd, char *buf, int len)
{
    (void)fd;
    (void)buf;
    (void)len;
    errno = ENOSYS;
    return -1;
}

int _write(int fd, const char *buf, int len)
{
    (void)fd;
    (void)buf;
    (void)len;
    errno = ENOSYS;
    return -1;
}
