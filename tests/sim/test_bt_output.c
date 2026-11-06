/* Asynchronous Bluetooth output tests. SPDX-License-Identifier: MIT */
#include "bt_output.h"
#include "gc_codec.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>

static bt_output out;
static uint8_t input[GC_FRAME_MAX_WIRE];

static void test_callback_matching(void)
{
    assert(bt_output_event_matches(42, 42, 1));
    assert(bt_output_event_matches(42, 42, 0));
    assert(bt_output_event_matches(42, 0, 0));
    assert(!bt_output_event_matches(42, 0, 1));
    assert(!bt_output_event_matches(42, 43, 0));
    assert(!bt_output_event_matches(0, 0, 0));
}

static void test_window_and_completions(void)
{
    size_t i;
    for (i = 0; i < sizeof(input); ++i) {
        input[i] = (uint8_t)(i % 251);
    }
    bt_output_reset(&out);
    for (i = 0; i < 4; ++i) {
        assert(bt_output_append(&out, input, sizeof(input)));
    }
    assert(out.queue.len == 4 * sizeof(input));
    assert(bt_output_begin(&out, 512) == 512);
    assert(bt_output_begin(&out, 512) == 0);
    assert(!bt_output_complete(&out, 0));
    assert(!bt_output_complete(&out, 513));
    assert(out.queue.len == 4 * sizeof(input));
    assert(bt_output_complete(&out, 100));
    assert(!bt_output_complete(&out, 1));
    assert(out.queue.len == 4 * sizeof(input) - 100);
    assert(memcmp(out.queue.data + out.queue.head, input + 100, 512 - 100) == 0);
    assert(bt_output_begin(&out, 512) == 512);
    bt_output_reset(&out);
    assert(out.inflight == 0 && out.queue.len == 0);
}

static void test_overflow_and_wrap(void)
{
    size_t i;
    bt_output_reset(&out);
    for (i = 0; i < TCP_OUTPUT_CAPACITY / sizeof(input); ++i) {
        assert(bt_output_append(&out, input, sizeof(input)));
    }
    assert(!bt_output_append(&out, input, sizeof(input)));
    assert(out.queue.len == i * sizeof(input));
    while (out.queue.len > 20) {
        size_t n = bt_output_begin(&out, 512);
        assert(n != 0);
        assert(bt_output_complete(&out, n));
    }
    assert(out.queue.len == 0);
    assert(bt_output_append(&out, input, sizeof(input)));
    i = 0;
    while (out.queue.len) {
        size_t n = bt_output_begin(&out, 512);
        assert(n != 0);
        assert(memcmp(out.queue.data + out.queue.head, input + i, n) == 0);
        assert(bt_output_complete(&out, n));
        i += n;
    }
    assert(i == sizeof(input));
}

int main(void)
{
    test_callback_matching();
    test_window_and_completions();
    test_overflow_and_wrap();
    puts("Bluetooth output tests passed");
    return 0;
}
