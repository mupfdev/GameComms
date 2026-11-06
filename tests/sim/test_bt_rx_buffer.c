/* Tests for the ESP32 Bluetooth receive buffer. SPDX-License-Identifier: MIT */
#include "../../protocol/tests/gc_test.h"
#include "bt_rx_buffer.h"

static bt_rx_buffer b;

static void test_buffer(void)
{
    uint8_t input[BT_RX_BUFFER_SIZE + 10], output[BT_RX_BUFFER_SIZE];
    size_t i;
    memset(&b, 0, sizeof(b));
    for (i = 0; i < sizeof(input); i++) {
        input[i] = (uint8_t)i;
    }
    CHECK_EQ(bt_rx_write(&b, input, 3000), 3000);
    CHECK_EQ(bt_rx_read(&b, 0, output, 2500), 2500);
    CHECK(memcmp(input, output, 2500) == 0);
    CHECK_EQ(bt_rx_write(&b, input, BT_RX_BUFFER_SIZE), BT_RX_BUFFER_SIZE - 500);
    CHECK_EQ(b.dropped, 500);
    CHECK_EQ(bt_rx_read(&b, 1, output, 1), 0);
    CHECK_EQ(bt_rx_read(&b, 0, output, sizeof(output)), BT_RX_BUFFER_SIZE);
    CHECK(memcmp(output, input + 2500, 500) == 0);
    CHECK(memcmp(output + 500, input, BT_RX_BUFFER_SIZE - 500) == 0);
    bt_rx_write(&b, input, 100);
    bt_rx_reset(&b);
    CHECK_EQ(bt_rx_read(&b, 0, output, 100), 0);
    CHECK_EQ(bt_rx_write(&b, input, 100), 100);
    CHECK_EQ(bt_rx_read(&b, 1, output, 100), 100);
    CHECK(memcmp(output, input, 100) == 0);
}

int main(void)
{
    RUN(test_buffer);
    TEST_MAIN_END();
}
