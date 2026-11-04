/** @file pcb_com_posix.c
 *
 *  POSIX serial port access for the PC bridge's COM mode (used by the
 *  automated tests with a pseudo terminal).
 *
 *  Copyright (c) 2023-2026, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

/* cfmakeraw() is a BSD/GNU extension */
#define _DEFAULT_SOURCE
#include "pcb_com.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

struct pcb_com
{
    int fd;
};

pcb_com *pcb_com_open(const char *name, char *err, size_t errlen)
{
    struct termios t;
    pcb_com *c;
    int fd = open(name, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        snprintf(err, errlen, "cannot open %s (%s)", name, strerror(errno));
        return NULL;
    }
    if (tcgetattr(fd, &t) == 0) {
        cfmakeraw(&t);
        tcsetattr(fd, TCSANOW, &t);
    }
    c = (pcb_com *)calloc(1, sizeof(*c));
    if (!c) {
        close(fd);
        snprintf(err, errlen, "out of memory");
        return NULL;
    }
    c->fd = fd;
    return c;
}

int pcb_com_read(pcb_com *c, unsigned char *buf, int max)
{
    ssize_t n = read(c->fd, buf, (size_t)max);
    if (n < 0) {
        return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR || errno == EIO) ? 0 : -1;
    }
    return (int)n;
}

int pcb_com_write(pcb_com *c, const unsigned char *buf, int len)
{
    ssize_t n = write(c->fd, buf, (size_t)len);
    if (n < 0) {
        return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR || errno == EIO) ? 0 : -1;
    }
    return (int)n;
}

int pcb_com_lines(pcb_com *c)
{
    (void)c;
    return -1;
}

void pcb_com_close(pcb_com *c)
{
    if (c) {
        close(c->fd);
        free(c);
    }
}
