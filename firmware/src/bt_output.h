/* Bounded asynchronous SPP output. SPDX-License-Identifier: MIT */
#ifndef BT_OUTPUT_H
#define BT_OUTPUT_H

#include "tcp_output.h"

typedef struct bt_output {
    tcp_output queue;
    size_t inflight;
} bt_output;

/* Bluedroid may report failed writes with a zero handle. */
static inline int bt_output_event_matches(uint32_t current, uint32_t event, int success)
{
    return current != 0 && (event == current || (event == 0 && !success));
}

static inline void bt_output_reset(bt_output *out)
{
    tcp_output_clear(&out->queue);
    out->inflight = 0;
}

static inline int bt_output_append(bt_output *out, const uint8_t *data, size_t len)
{
    return tcp_output_append(&out->queue, data, len);
}

/* The queue retains the submitted bytes until the completion event. */
static inline size_t bt_output_begin(bt_output *out, size_t limit)
{
    size_t n;
    if (out->inflight || !limit) {
        return 0;
    }
    n = tcp_output_chunk(&out->queue);
    if (n > limit) {
        n = limit;
    }
    out->inflight = n;
    return n;
}

static inline int bt_output_complete(bt_output *out, size_t len)
{
    if (!out->inflight || !len || len > out->inflight) {
        return 0;
    }
    tcp_output_consume(&out->queue, len);
    out->inflight = 0;
    return 1;
}

#endif
