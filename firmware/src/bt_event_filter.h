/* Track the SPP connection accepted by BluetoothSerial. */
#ifndef BT_EVENT_FILTER_H
#define BT_EVENT_FILTER_H

#include <stdint.h>

typedef struct
{
    uint32_t active_handle;
} bt_event_filter;

static inline int bt_event_open(bt_event_filter *filter, uint32_t handle, int success)
{
    if (!success || !handle || filter->active_handle) {
        return 0;
    }
    filter->active_handle = handle;
    return 1;
}

static inline int bt_event_close(bt_event_filter *filter, uint32_t handle, int success, int asynchronous)
{
    if (!handle || handle != filter->active_handle || (!success && !asynchronous)) {
        return 0;
    }
    filter->active_handle = 0;
    return 1;
}

#endif
