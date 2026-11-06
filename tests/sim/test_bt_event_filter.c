/* Tests for ESP32 SPP connection event filtering. SPDX-License-Identifier: MIT */
#include "../../protocol/tests/gc_test.h"
#include "bt_event_filter.h"

static void test_connection_events(void)
{
    bt_event_filter f = { 0 };
    CHECK(!bt_event_open(&f, 1, 0));
    CHECK(!bt_event_open(&f, 0, 1));
    CHECK(bt_event_open(&f, 1, 1));
    CHECK(!bt_event_open(&f, 2, 1));
    CHECK(!bt_event_close(&f, 2, 1, 1));
    CHECK(!bt_event_close(&f, 1, 0, 0));
    CHECK_EQ(f.active_handle, 1);
    CHECK(bt_event_close(&f, 1, 0, 1));
    CHECK_EQ(f.active_handle, 0);
    CHECK(!bt_event_close(&f, 1, 1, 1));
    CHECK(bt_event_open(&f, 2, 1));
    CHECK(bt_event_close(&f, 2, 1, 0));
    CHECK_EQ(f.active_handle, 0);
}

int main(void)
{
    RUN(test_connection_events);
    TEST_MAIN_END();
}
