/* Bounded TCP byte queue. SPDX-License-Identifier: MIT */
#ifndef TCP_OUTPUT_H
#define TCP_OUTPUT_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define TCP_OUTPUT_CAPACITY 8192

typedef struct tcp_output
{
    uint8_t data[TCP_OUTPUT_CAPACITY];
    size_t head;
    size_t len;
} tcp_output;

static inline void tcp_output_clear(tcp_output *q)
{
    q->head = 0;
    q->len = 0;
}

static inline int tcp_output_append(tcp_output *q, const uint8_t *data, size_t len)
{
    size_t tail, first;
    if (len > TCP_OUTPUT_CAPACITY - q->len) {
        return 0;
    }
    tail = (q->head + q->len) % TCP_OUTPUT_CAPACITY;
    first = len < TCP_OUTPUT_CAPACITY - tail ? len : TCP_OUTPUT_CAPACITY - tail;
    memcpy(q->data + tail, data, first);
    memcpy(q->data, data + first, len - first);
    q->len += len;
    return 1;
}

static inline size_t tcp_output_chunk(const tcp_output *q)
{
    return q->len < TCP_OUTPUT_CAPACITY - q->head ? q->len : TCP_OUTPUT_CAPACITY - q->head;
}

static inline void tcp_output_consume(tcp_output *q, size_t len)
{
    q->head = (q->head + len) % TCP_OUTPUT_CAPACITY;
    q->len -= len;
}

#endif
