/** ESP32/lwIP nonblocking server transport. SPDX-License-Identifier: MIT */
#include "async_server.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ASYNC_SERVER_TEST
#include "async_server_test_platform.h"
#else
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#include <lwip/dns.h>
#include <lwip/inet.h>
#include <lwip/sockets.h>
#include <lwip/tcpip.h>
#endif

namespace
{
const uint32_t discovery_timeout = 800;
const uint32_t dns_timeout = 5000;
const uint32_t connect_timeout = 3000;
const size_t max_send_per_poll = 512;

enum DnsState
{
    DNS_FREE,
    DNS_QUEUED,
    DNS_WORKING,
    DNS_COMPLETED_WORKING,
    DNS_WAITING,
    DNS_DONE
};
struct DnsSlot
{
    DnsState state;
    bool canceled;
    bool success;
    uint32_t address;
    char host[254];
};
/* Slots live independently of AsyncServer: a canceled lwIP request may still
   call back much later, even after the owning object has been destroyed. */
DnsSlot dns_slots[4] = {};
portMUX_TYPE dns_mux = portMUX_INITIALIZER_UNLOCKED;

void dns_complete(DnsSlot *slot, const ip_addr_t *address)
{
    portENTER_CRITICAL(&dns_mux);
    slot->success = address && IP_IS_V4(address);
    slot->address = slot->success ? ip_addr_get_ip4_u32(address) : 0;
    if (slot->state == DNS_WORKING) {
        /* dns_start still owns the slot until the resolver returns. */
        slot->state = DNS_COMPLETED_WORKING;
    } else {
        slot->state = slot->canceled ? DNS_FREE : DNS_DONE;
    }
    portEXIT_CRITICAL(&dns_mux);
}

void dns_found(const char *, const ip_addr_t *address, void *arg)
{
    dns_complete(static_cast<DnsSlot *>(arg), address);
}

void dns_start(void *arg)
{
    DnsSlot *slot = static_cast<DnsSlot *>(arg);
    portENTER_CRITICAL(&dns_mux);
    if (slot->canceled) {
        slot->state = DNS_FREE;
        portEXIT_CRITICAL(&dns_mux);
        return;
    }
    slot->state = DNS_WORKING;
    portEXIT_CRITICAL(&dns_mux);

    ip_addr_t address;
    err_t rc = dns_gethostbyname_addrtype(slot->host, &address, dns_found, slot, LWIP_DNS_ADDRTYPE_IPV4);
    portENTER_CRITICAL(&dns_mux);
    if (slot->state == DNS_COMPLETED_WORKING) {
        slot->state = slot->canceled ? DNS_FREE : DNS_DONE;
    } else if (rc == ERR_INPROGRESS) {
        slot->state = DNS_WAITING;
    } else {
        slot->success = rc == ERR_OK && IP_IS_V4(&address);
        slot->address = slot->success ? ip_addr_get_ip4_u32(&address) : 0;
        slot->state = slot->canceled ? DNS_FREE : DNS_DONE;
    }
    portEXIT_CRITICAL(&dns_mux);
}

bool wouldBlock(int error)
{
    return error == EAGAIN || error == EWOULDBLOCK || error == EINTR || error == EINPROGRESS || error == EALREADY;
}
} // namespace

AsyncServer::AsyncServer() : state_(IDLE), fd_(-1), dns_slot_(-1), started_(0), port_(0),
                             discovery_port_(0), tx_head_(0), tx_count_(0), event_{ NONE, OK, 0 }
{
    host_[0] = 0;
    memset(broadcast_, 0, sizeof(broadcast_));
}

AsyncServer::~AsyncServer() { stop(); }

void AsyncServer::closeSocket()
{
    if (fd_ >= 0) {
        closesocket(fd_);
        fd_ = -1;
    }
}

void AsyncServer::cancelDns()
{
    if (dns_slot_ < 0) {
        return;
    }
    portENTER_CRITICAL(&dns_mux);
    DnsSlot &slot = dns_slots[dns_slot_];
    if (slot.state == DNS_DONE) {
        slot.state = DNS_FREE;
    } else {
        slot.canceled = true;
    }
    portEXIT_CRITICAL(&dns_mux);
    dns_slot_ = -1;
}

void AsyncServer::stop()
{
    cancelDns();
    closeSocket();
    state_ = IDLE;
    tx_head_ = tx_count_ = 0;
    event_ = { NONE, OK, 0 };
}

void AsyncServer::fail(Error error, int system_error)
{
    cancelDns();
    closeSocket();
    state_ = IDLE;
    tx_head_ = tx_count_ = 0;
    event_ = { DISCONNECTED, error, system_error };
}

bool AsyncServer::openSocket(int type)
{
    fd_ = socket(AF_INET, type, 0);
    if (fd_ < 0) {
        fail(SOCKET_ERROR, errno);
        return false;
    }
    int flags = fcntl(fd_, F_GETFL, 0);
    if (flags < 0 || fcntl(fd_, F_SETFL, flags | O_NONBLOCK) < 0) {
        fail(SOCKET_ERROR, errno);
        return false;
    }
    return true;
}

bool AsyncServer::beginConnect(uint32_t address, uint16_t port, uint32_t now)
{
    closeSocket();
    if (!openSocket(SOCK_STREAM)) {
        return false;
    }
    int one = 1;
    if (setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)) < 0) {
        fail(SOCKET_ERROR, errno);
        return false;
    }
    sockaddr_in peer = {};
    peer.sin_family = AF_INET;
    peer.sin_port = htons(port);
    peer.sin_addr.s_addr = address;
    int rc = connect(fd_, reinterpret_cast<sockaddr *>(&peer), sizeof(peer));
    if (rc == 0) {
        state_ = ONLINE;
        event_ = { CONNECTED, OK, 0 };
    } else if (wouldBlock(errno)) {
        state_ = CONNECTING;
        started_ = now;
    } else {
        fail(SOCKET_ERROR, errno);
        return false;
    }
    return true;
}

AsyncServer::Error AsyncServer::start(const char *host, uint16_t port, uint16_t discovery_port,
                                      const uint8_t broadcast[4], uint32_t now)
{
    if (state_ != IDLE || event_.event != NONE) {
        return BUSY;
    }
    if (!host) {
        return INVALID_ARGUMENT;
    }
    size_t host_len = strlen(host);
    if (host_len >= sizeof(host_)) {
        return INVALID_ARGUMENT;
    }
    bool discovery = !host[0] || strcmp(host, "auto") == 0;
    if ((discovery && (!broadcast || !discovery_port)) || (!discovery && !port)) {
        return INVALID_ARGUMENT;
    }
    memcpy(host_, host, host_len + 1);
    port_ = port;
    discovery_port_ = discovery_port;
    started_ = now;
    if (discovery) {
        memcpy(broadcast_, broadcast, 4);
        if (!openSocket(SOCK_DGRAM)) {
            return SOCKET_ERROR;
        }
        int one = 1;
        if (setsockopt(fd_, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one)) < 0) {
            int error = errno;
            fail(SOCKET_ERROR, error);
            return SOCKET_ERROR;
        }
        sockaddr_in peer = {};
        peer.sin_family = AF_INET;
        peer.sin_port = htons(discovery_port_);
        memcpy(&peer.sin_addr.s_addr, broadcast_, 4);
        static const char request[] = "GCDISCOVER1";
        int n = sendto(fd_, request, sizeof(request) - 1, 0, reinterpret_cast<sockaddr *>(&peer), sizeof(peer));
        if (n != static_cast<int>(sizeof(request) - 1)) {
            int error = n < 0 ? errno : 0;
            fail(SOCKET_ERROR, error);
            return SOCKET_ERROR;
        }
        state_ = DISCOVER;
        return OK;
    }
    in_addr address;
    if (inet_pton(AF_INET, host_, &address) == 1) {
        return beginConnect(address.s_addr, port_, now) ? OK : SOCKET_ERROR;
    }
    portENTER_CRITICAL(&dns_mux);
    for (size_t i = 0; i < sizeof(dns_slots) / sizeof(dns_slots[0]); ++i) {
        if (dns_slots[i].state == DNS_FREE) {
            dns_slot_ = static_cast<int>(i);
            dns_slots[i].state = DNS_QUEUED;
            dns_slots[i].canceled = false;
            memcpy(dns_slots[i].host, host_, host_len + 1);
            break;
        }
    }
    portEXIT_CRITICAL(&dns_mux);
    if (dns_slot_ < 0) {
        fail(DNS_BUSY);
        return DNS_BUSY;
    }
    if (tcpip_try_callback(dns_start, &dns_slots[dns_slot_]) != ERR_OK) {
        portENTER_CRITICAL(&dns_mux);
        dns_slots[dns_slot_].state = DNS_FREE;
        portEXIT_CRITICAL(&dns_mux);
        dns_slot_ = -1;
        fail(DNS_BUSY);
        return DNS_BUSY;
    }
    state_ = RESOLVE;
    return OK;
}

AsyncServer::Result AsyncServer::takeEvent()
{
    Result result = event_;
    event_ = { NONE, OK, 0 };
    return result;
}

AsyncServer::Result AsyncServer::poll(uint32_t now)
{
    if (event_.event != NONE) {
        return takeEvent();
    }
    if (state_ == DISCOVER) {
        char response[96];
        sockaddr_in from = {};
        socklen_t from_len = sizeof(from);
        int n = recvfrom(fd_, response, sizeof(response) - 1, 0, reinterpret_cast<sockaddr *>(&from), &from_len);
        if (n > 0) {
            response[n] = 0;
            const char prefix[] = "GCSERVER1 ";
            if (strncmp(response, prefix, sizeof(prefix) - 1) == 0) {
                char *end;
                const char *number = response + sizeof(prefix) - 1;
                unsigned long port = strtoul(number, &end, 10);
                if (number != end && *number >= '0' && *number <= '9' && port > 0 && port <= 65535 &&
                    (*end == 0 || *end == ' ' || *end == '\r' || *end == '\n') && from.sin_family == AF_INET) {
                    beginConnect(from.sin_addr.s_addr, static_cast<uint16_t>(port), now);
                }
            }
        } else if (n < 0 && !wouldBlock(errno)) {
            fail(SOCKET_ERROR, errno);
        }
        if (state_ == DISCOVER && static_cast<uint32_t>(now - started_) >= discovery_timeout) {
            fail(DISCOVERY_TIMEOUT);
        }
    } else if (state_ == RESOLVE) {
        bool done = false, success = false;
        uint32_t address = 0;
        portENTER_CRITICAL(&dns_mux);
        DnsSlot &slot = dns_slots[dns_slot_];
        if (slot.state == DNS_DONE) {
            done = true;
            success = slot.success;
            address = slot.address;
            slot.state = DNS_FREE;
        }
        portEXIT_CRITICAL(&dns_mux);
        if (done) {
            dns_slot_ = -1;
            if (success) {
                beginConnect(address, port_, now);
            } else {
                fail(DNS_FAILED);
            }
        } else if (static_cast<uint32_t>(now - started_) >= dns_timeout) {
            fail(DNS_TIMEOUT);
        }
    } else if (state_ == CONNECTING) {
        fd_set writes, errors;
        FD_ZERO(&writes);
        FD_ZERO(&errors);
        FD_SET(fd_, &writes);
        FD_SET(fd_, &errors);
        timeval timeout = {};
        int n = select(fd_ + 1, NULL, &writes, &errors, &timeout);
        if (n < 0 && errno != EINTR) {
            fail(SOCKET_ERROR, errno);
        } else if (n > 0) {
            int error = 0;
            socklen_t len = sizeof(error);
            if (getsockopt(fd_, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
                fail(SOCKET_ERROR, errno);
            } else if (error) {
                fail(SOCKET_ERROR, error);
            } else {
                state_ = ONLINE;
                event_ = { CONNECTED, OK, 0 };
            }
        } else if (static_cast<uint32_t>(now - started_) >= connect_timeout) {
            fail(CONNECT_TIMEOUT);
        }
    } else if (state_ == ONLINE && tx_count_) {
        size_t n = ASYNC_SERVER_TX_CAPACITY - tx_head_;
        if (n > tx_count_) {
            n = tx_count_;
        }
        if (n > max_send_per_poll) {
            n = max_send_per_poll;
        }
        int sent = send(fd_, tx_ + tx_head_, n, 0);
        if (sent > 0) {
            tx_head_ = (tx_head_ + static_cast<size_t>(sent)) % ASYNC_SERVER_TX_CAPACITY;
            tx_count_ -= static_cast<size_t>(sent);
        } else if (sent == 0) {
            fail(PEER_CLOSED);
        } else if (!wouldBlock(errno)) {
            fail(SOCKET_ERROR, errno);
        }
    }
    return takeEvent();
}

AsyncServer::Error AsyncServer::write(const uint8_t *data, size_t len)
{
    if (state_ != ONLINE) {
        return NOT_CONNECTED;
    }
    if ((!data && len) || len > ASYNC_SERVER_TX_CAPACITY - tx_count_) {
        Error error = (!data && len) ? INVALID_ARGUMENT : QUEUE_FULL;
        fail(error);
        return error;
    }
    size_t tail = (tx_head_ + tx_count_) % ASYNC_SERVER_TX_CAPACITY;
    size_t first = ASYNC_SERVER_TX_CAPACITY - tail;
    if (first > len) {
        first = len;
    }
    if (first) {
        memcpy(tx_ + tail, data, first);
    }
    if (len > first) {
        memcpy(tx_, data + first, len - first);
    }
    tx_count_ += len;
    return OK;
}

int AsyncServer::read(uint8_t *data, size_t capacity)
{
    if (state_ != ONLINE) {
        return -1;
    }
    if (!data || !capacity) {
        return 0;
    }
    if (capacity > 512) {
        capacity = 512;
    }
    int n = recv(fd_, data, capacity, 0);
    if (n > 0) {
        return n;
    }
    if (n == 0) {
        fail(PEER_CLOSED);
    } else if (!wouldBlock(errno)) {
        fail(SOCKET_ERROR, errno);
    } else {
        return 0;
    }
    return -1;
}

bool AsyncServer::connected() const { return state_ == ONLINE; }
