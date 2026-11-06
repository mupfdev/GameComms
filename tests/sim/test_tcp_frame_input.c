/* ESP32 TCP frame pacing regression. SPDX-License-Identifier: MIT */
#include "bridge_core.h"
#include "tcp_frame_input.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define FRAMES 16
static bridge b;
static gc_deframer output;
static unsigned delivered;

static void frame_received(void *ctx, const gc_u8 *body, size_t len)
{
    (void)ctx;
    if (len >= 8 && body[0] == GC_FT_DATA) {
        assert(body[6] == GC_MT_DATA);
        assert(body[7] == delivered);
        ++delivered;
    }
}

static void dev_write(void *ctx, const gc_u8 *data, size_t len)
{
    (void)ctx;
    gc_deframer_feed(&output, data, len);
}

static void init_bridge(void)
{
    bridge_cfg cfg;
    bridge_io io;
    memset(&io, 0, sizeof(io));
    io.dev_write = dev_write;
    bridge_default_cfg(&cfg);
    bridge_init(&b, &cfg, &io, NULL);
    b.dev_connected = 1;
    b.dev.state = GC_LS_UP;
    b.srv.state = GC_LS_UP;
    b.dev.t_last_rx = b.dev.t_last_tx = 1000;
    b.srv.t_last_rx = b.srv.t_last_tx = 1000;
    delivered = 0;
    gc_deframer_init(&output, frame_received, NULL);
}

static size_t make_frames(gc_u8 *buf, size_t cap)
{
    size_t used = 0;
    unsigned i;
    for (i = 0; i < FRAMES; ++i) {
        gc_u8 body[] = { GC_FT_DATA, 1, 0, (gc_u8)i, 0, 0, GC_MT_DATA, (gc_u8)i };
        size_t n = gc_frame_encode(body, sizeof(body), buf + used, cap - used);
        assert(n != 0);
        used += n;
    }
    return used;
}

static void acknowledge_device(void)
{
    gc_u16 ack = b.dev.tx_next;
    gc_u8 body[] = { GC_FT_ACK, 1, 0, 0, (gc_u8)(ack >> 8), (gc_u8)ack };
    gc_u8 frame[GC_FRAME_MAX_WIRE];
    size_t n = gc_frame_encode(body, sizeof(body), frame, sizeof(frame));
    assert(n != 0);
    bridge_dev_input(&b, frame, n, 1000);
    bridge_poll(&b, 1000);
}

static void test_paced(const gc_u8 *stream, size_t len, size_t read_limit)
{
    size_t pos = 0;
    int pauses = 0;
    tcp_frame_pause pause = {0};
    init_bridge();
    while (pos < len) {
        size_t n;
        if (!tcp_frame_input_ready(&pause, b.dev_connected && b.dev.state == GC_LS_UP,
                                   b.to_dev.count, BRIDGE_FIFO_LEN, b.srv.q_sent, 1000)) {
            assert(++pauses <= FRAMES);
            acknowledge_device();
            continue;
        }
        n = len - pos;
        if (n > read_limit) {
            n = read_limit;
        }
        n = stream_frame_input_chunk(stream + pos, n);
        assert(n > 0);
        bridge_srv_input(&b, stream + pos, n, 1000);
        pos += n;
    }
    while (delivered < FRAMES) {
        assert(++pauses <= FRAMES);
        acknowledge_device();
    }
    assert(pauses > 0);
    assert(delivered == FRAMES);
    assert(b.srv.st_backpressure == 0);
    assert(b.srv.st_ooo == 0);
}

static void test_server_ack_when_device_fifo_full(const gc_u8 *stream, size_t len)
{
    tcp_frame_pause pause = {0};
    gc_u8 msg[] = { GC_MT_DATA, 0 };
    gc_u8 ack[] = { GC_FT_ACK, 1, 0, 0, 0, GC_LINK_QUEUE };
    gc_u8 frame[GC_FRAME_MAX_WIRE];
    size_t pos = 0, n, blocked_pos;
    int i;
    init_bridge();
    while (b.to_dev.count < BRIDGE_FIFO_LEN) {
        assert(pos < len);
        n = stream_frame_input_chunk(stream + pos, len - pos);
        bridge_srv_input(&b, stream + pos, n, 1000);
        pos += n;
    }
    assert(b.dev.q_count == GC_LINK_QUEUE);
    for (i = 0; i < GC_LINK_QUEUE; ++i) {
        assert(gc_link_send(&b.srv, msg, sizeof(msg), 1000) == GC_LINK_OK);
    }
    b.to_srv.count = BRIDGE_FIFO_LEN;
    for (i = 0; i < BRIDGE_FIFO_LEN; ++i) {
        b.to_srv.s[i].len = sizeof(msg);
        memcpy(b.to_srv.s[i].data, msg, sizeof(msg));
    }
    blocked_pos = pos;
    do {
        assert(tcp_frame_input_ready(&pause, 1, b.to_dev.count, BRIDGE_FIFO_LEN, b.srv.q_sent, 1000));
        n = stream_frame_input_chunk(stream + pos, len - pos);
        assert(n > 0);
        bridge_srv_input(&b, stream + pos, n, 1000);
        pos += n;
    } while (b.srv.st_backpressure == 0);
    n = gc_frame_encode(ack, sizeof(ack), frame, sizeof(frame));
    assert(n != 0);
    assert(tcp_frame_input_ready(&pause, 1, b.to_dev.count, BRIDGE_FIFO_LEN, b.srv.q_sent, 1000));
    bridge_srv_input(&b, frame, n, 1000);
    assert(b.to_dev.count == BRIDGE_FIFO_LEN);
    bridge_poll(&b, 1000);
    assert(b.to_srv.count == 0);
    assert(b.srv.q_count == GC_LINK_QUEUE);
    acknowledge_device();
    assert(b.to_dev.count == 0);
    bridge_srv_input(&b, stream + blocked_pos, pos - blocked_pos, 1000);
    assert(b.srv.rx_next == 9);
    assert(b.to_dev.count == 1);
}

static void test_pause_timeout(void)
{
    tcp_frame_pause pause = {0};
    assert(!tcp_frame_input_ready(&pause, 1, 4, 4, 0, UINT32_MAX - 49));
    assert(!tcp_frame_input_ready(&pause, 1, 4, 4, 0, 49));
    assert(tcp_frame_input_ready(&pause, 1, 4, 4, 0, 50));
    assert(!tcp_frame_input_ready(&pause, 1, 4, 4, 0, 51));
    assert(tcp_frame_input_ready(&pause, 0, 4, 4, 0, 51));
    assert(!tcp_frame_input_ready(&pause, 1, 4, 4, 0, 51));
    assert(tcp_frame_input_ready(&pause, 1, 4, 4, 1, 52));
}

static void test_bluetooth_frame_draining(const gc_u8 *stream, size_t len, int paced)
{
    gc_u8 msg[] = { GC_MT_DATA, 0 };
    size_t pos = 0;
    int i;
    init_bridge();
    b.srv_state = GC_BS_UP;
    for (i = 0; i < 2; ++i) {
        assert(gc_link_send(&b.srv, msg, sizeof(msg), 1000) == GC_LINK_OK);
    }
    b.to_srv.count = 3;
    for (i = 0; i < b.to_srv.count; ++i) {
        b.to_srv.s[i].len = sizeof(msg);
        memcpy(b.to_srv.s[i].data, msg, sizeof(msg));
    }
    if (!paced) {
        bridge_dev_input(&b, stream, len, 1000);
        assert(b.dev.st_backpressure == 1);
        assert(b.dev.rx_next == 1);
        return;
    }
    while (pos < len) {
        size_t n = stream_frame_input_chunk(stream + pos, len - pos);
        assert(n > 0);
        bridge_dev_input(&b, stream + pos, n, 1000);
        pos += n;
    }
    assert(b.dev.st_backpressure == 0);
    assert(b.dev.rx_next == 2);
    assert(b.to_srv.count == 3);
}

int main(void)
{
    gc_u8 stream[FRAMES * 32];
    size_t len = make_frames(stream, sizeof(stream));
    size_t two_frames = 0;
    int boundaries = 0;
    init_bridge();
    bridge_srv_input(&b, stream, len, 1000);
    assert(b.srv.st_backpressure > 0);
    assert(b.srv.st_ooo > 0);
    test_paced(stream, len, 1);
    test_paced(stream, len, 7);
    test_paced(stream, len, 512);
    test_server_ack_when_device_fifo_full(stream, len);
    test_pause_timeout();
    while (two_frames < len && boundaries < 4) {
        if (stream[two_frames++] == 0) {
            ++boundaries;
        }
    }
    assert(boundaries == 4);
    test_bluetooth_frame_draining(stream, two_frames, 0);
    test_bluetooth_frame_draining(stream, two_frames, 1);
    puts("TCP frame pacing tests passed");
    return 0;
}
