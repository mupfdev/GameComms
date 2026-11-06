/* Select at most one stream frame boundary per TCP read. SPDX-License-Identifier: MIT */
#ifndef TCP_FRAME_INPUT_H
#define TCP_FRAME_INPUT_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t since;
    int paused;
} tcp_frame_pause;

static inline int tcp_frame_input_ready(tcp_frame_pause *pause, int forwarding, int queued, int capacity,
                                        int server_outstanding, uint32_t now)
{
    if (!forwarding || queued < capacity || server_outstanding) {
        pause->paused = 0;
        return 1;
    }
    if (!pause->paused) {
        pause->paused = 1;
        pause->since = now;
        return 0;
    }
    if ((uint32_t)(now - pause->since) >= 100) {
        pause->since = now;
        return 1;
    }
    return 0;
}

static inline size_t stream_frame_input_chunk(const uint8_t *data, size_t len)
{
    size_t i;
    for (i = 0; i < len; ++i) {
        if (data[i] == 0) {
            return i + 1;
        }
    }
    return len;
}

#endif
