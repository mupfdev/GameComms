/** ESP32 server transport: asynchronous discovery, DNS and TCP, with a
 *  bounded FIFO for complete frames and deferred connection/loss events.
 *  Call from one loop task; see method contracts below.
 *  SPDX-License-Identifier: MIT
 */
#ifndef ASYNC_SERVER_H
#define ASYNC_SERVER_H

#include <stddef.h>
#include <stdint.h>

/* Fixed per-instance memory; 8 KiB accommodates four maximum wire frames and
   ACK bursts. Override consistently across translation units if necessary. */
#ifndef ASYNC_SERVER_TX_CAPACITY
#define ASYNC_SERVER_TX_CAPACITY 8192
#endif

class AsyncServer
{
  public:
    enum Error
    {
        OK,
        INVALID_ARGUMENT,
        BUSY,
        NOT_CONNECTED,
        QUEUE_FULL,
        DNS_BUSY,
        DNS_FAILED,
        DNS_TIMEOUT,
        DISCOVERY_TIMEOUT,
        CONNECT_TIMEOUT,
        SOCKET_ERROR,
        PEER_CLOSED
    };
    enum Event
    {
        NONE,
        CONNECTED,
        DISCONNECTED
    };
    struct Result
    {
        Event event;
        Error error;
        int system_error;
    };

    AsyncServer();
    ~AsyncServer();
    AsyncServer(const AsyncServer &) = delete;
    AsyncServer &operator=(const AsyncServer &) = delete;

    /* host is copied. Empty or "auto" uses discovery on discovery_port.
       broadcast is the four network-order bytes of the subnet broadcast.
       BUSY leaves the current attempt intact; INVALID_ARGUMENT emits no event.
       Other immediate failures also produce one DISCONNECTED event. */
    Error start(const char *host, uint16_t port, uint16_t discovery_port,
                const uint8_t broadcast[4], uint32_t now);
    /* Cancellation is silent; it also discards queued events and bytes. */
    void stop();
    /* Call frequently; deliver events to the bridge outside its callbacks.
       On CONNECTED call bridge_srv_connected; on DISCONNECTED call
       bridge_srv_disconnected exactly once. system_error contains lwIP errno. */
    Result poll(uint32_t now);
    /* All-or-nothing. A full queue or invalid frame on an open connection
       closes it and generates DISCONNECTED; NOT_CONNECTED accepts nothing. */
    Error write(const uint8_t *data, size_t len);
    /* Nonblocking, up to capacity. Returns bytes, 0 if no data, -1 on loss.
       Forward bytes to bridge_srv_input; a loss is delivered by the next poll.
       Read regularly even when idle so remote close is detected. */
    int read(uint8_t *data, size_t capacity);
    bool connected() const;
    size_t queued() const { return tx_count_; }

  private:
    enum State
    {
        IDLE,
        DISCOVER,
        RESOLVE,
        CONNECTING,
        ONLINE
    };
    void fail(Error error, int system_error = 0);
    void closeSocket();
    bool openSocket(int type);
    bool beginConnect(uint32_t address, uint16_t port, uint32_t now);
    void cancelDns();
    Result takeEvent();

    State state_;
    int fd_;
    int dns_slot_;
    uint32_t started_;
    uint16_t port_;
    uint16_t discovery_port_;
    uint8_t broadcast_[4];
    char host_[254];
    uint8_t tx_[ASYNC_SERVER_TX_CAPACITY];
    size_t tx_head_, tx_count_;
    Result event_;
};
#endif
