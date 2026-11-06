/* Bounded byte buffer used by the Bluetooth callback and main loop. */
#ifndef BT_RX_BUFFER_H
#define BT_RX_BUFFER_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define BT_RX_BUFFER_SIZE 4096

typedef struct
{
    uint8_t data[BT_RX_BUFFER_SIZE];
    size_t head;
    size_t count;
    uint32_t generation;
    uint32_t dropped;
} bt_rx_buffer;

static inline void bt_rx_reset(bt_rx_buffer *b)
{
    b->head = 0;
    b->count = 0;
    b->generation++;
}

static inline size_t bt_rx_write(bt_rx_buffer *b, const uint8_t *data, size_t len)
{
    size_t n = len;
    size_t tail = (b->head + b->count) % BT_RX_BUFFER_SIZE;
    size_t first;
    if (n > BT_RX_BUFFER_SIZE - b->count) {
        n = BT_RX_BUFFER_SIZE - b->count;
    }
    first = n < BT_RX_BUFFER_SIZE - tail ? n : BT_RX_BUFFER_SIZE - tail;
    memcpy(b->data + tail, data, first);
    memcpy(b->data, data + first, n - first);
    b->count += n;
    b->dropped += (uint32_t)(len - n);
    return n;
}

static inline size_t bt_rx_read(bt_rx_buffer *b, uint32_t generation, uint8_t *data, size_t len)
{
    size_t first;
    if (b->generation != generation) {
        return 0;
    }
    if (len > b->count) {
        len = b->count;
    }
    first = len < BT_RX_BUFFER_SIZE - b->head ? len : BT_RX_BUFFER_SIZE - b->head;
    memcpy(data, b->data + b->head, first);
    memcpy(data + first, b->data, len - first);
    b->head = (b->head + len) % BT_RX_BUFFER_SIZE;
    b->count -= len;
    return len;
}

#endif
