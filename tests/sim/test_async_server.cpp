/* Standalone host test: compile this file only; it includes the implementation. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../../protocol/include/gc_codec.h"
#include <assert.h>
#include <string.h>
#include <string>
#include <vector>
#define ASYNC_SERVER_TEST
#include "../../firmware/src/async_server.cpp"

static int connect_rc, connect_error, select_rc, socket_error, send_limit = 512;
static int recv_mode, close_count;
static std::string sent_bytes, discovery_reply;
static void (*queued_callback)(void *);
static void *queued_arg;
static dns_found_callback found_callback;
static void *found_arg;
static bool dns_immediate;
static bool dns_callback_early;
static AsyncServer *dns_interleaved_server;
static bool dns_cancel_first;

int fake_socket(int, int, int) { return 5; }
int fake_closesocket(int)
{
    ++close_count;
    return 0;
}
int fake_fcntl(int, int, int) { return 0; }
int fake_setsockopt(int, int, int, const void *, socklen_t) { return 0; }
int fake_getsockopt(int, int, int, void *v, socklen_t *)
{
    *static_cast<int *>(v) = socket_error;
    return 0;
}
int fake_connect(int, const sockaddr *, socklen_t)
{
    errno = connect_error;
    return connect_rc;
}
int fake_sendto(int, const void *, size_t n, int, const sockaddr *, socklen_t) { return static_cast<int>(n); }
int fake_recvfrom(int, void *v, size_t n, int, sockaddr *from, socklen_t *)
{
    if (discovery_reply.empty()) {
        errno = EAGAIN;
        return -1;
    }
    if (n > discovery_reply.size()) {
        n = discovery_reply.size();
    }
    memcpy(v, discovery_reply.data(), n);
    sockaddr_in *address = reinterpret_cast<sockaddr_in *>(from);
    address->sin_family = AF_INET;
    address->sin_addr.s_addr = 42;
    discovery_reply.clear();
    return static_cast<int>(n);
}
int fake_send(int, const void *v, size_t n, int)
{
    if (!send_limit) {
        errno = EAGAIN;
        return -1;
    }
    if (n > static_cast<size_t>(send_limit)) {
        n = send_limit;
    }
    sent_bytes.append(static_cast<const char *>(v), n);
    return static_cast<int>(n);
}
int fake_recv(int, void *, size_t, int)
{
    if (!recv_mode) {
        errno = EAGAIN;
        return -1;
    }
    return 0;
}
int fake_select(int, fake_fd_set *, fake_fd_set *, fake_fd_set *, fake_timeval *) { return select_rc; }
int fake_inet_pton(int, const char *s, void *out)
{
    if (strcmp(s, "127.0.0.1")) {
        return 0;
    }
    static_cast<in_addr *>(out)->s_addr = 1;
    return 1;
}
uint16_t fake_htons(uint16_t x) { return x; }
err_t tcpip_try_callback(void (*fn)(void *), void *arg)
{
    queued_callback = fn;
    queued_arg = arg;
    return ERR_OK;
}
err_t dns_gethostbyname_addrtype(const char *, ip_addr_t *ip, dns_found_callback cb, void *arg, int)
{
    if (dns_interleaved_server) {
        AsyncServer *server = dns_interleaved_server;
        dns_interleaved_server = NULL;
        ip_addr_t result = { 99 };
        if (dns_cancel_first) {
            server->stop();
        }
        cb("host", &result, arg);
        if (!dns_cancel_first) {
            server->stop();
        }
        assert(server->start("new.test", 1, 0, NULL, 1) == AsyncServer::OK);
        assert(queued_arg != arg); /* old resolver invocation still owns its slot */
        return ERR_INPROGRESS;
    }
    if (dns_callback_early) {
        ip_addr_t result = { 99 };
        cb("host", &result, arg);
        return ERR_INPROGRESS;
    }
    if (dns_immediate) {
        ip->address = 99;
        return ERR_OK;
    }
    found_callback = cb;
    found_arg = arg;
    return ERR_INPROGRESS;
}
static void run_dns()
{
    void (*fn)(void *) = queued_callback;
    queued_callback = NULL;
    fn(queued_arg);
}
static void dns_result(bool success)
{
    ip_addr_t address = { 99 };
    dns_found_callback cb = found_callback;
    found_callback = NULL;
    cb("host", success ? &address : NULL, found_arg);
}
static void reset()
{
    connect_rc = connect_error = select_rc = socket_error = recv_mode = close_count = 0;
    send_limit = 512;
    sent_bytes.clear();
    discovery_reply.clear();
    dns_immediate = false;
    dns_callback_early = false;
    dns_interleaved_server = NULL;
    dns_cancel_first = false;
}
static void start_numeric(AsyncServer &s)
{
    assert(s.start("127.0.0.1", 9888, 0, NULL, 0) == AsyncServer::OK);
    assert(s.poll(0).event == AsyncServer::CONNECTED);
}

int main()
{
    reset();
    {
        AsyncServer s;
        start_numeric(s);
        std::string a(7000, 'a'), b(500, 'b'), c(1600, 'c');
        assert(s.write((const uint8_t *)a.data(), a.size()) == AsyncServer::OK);
        send_limit = 100;
        for (int i = 0; i < 10; ++i) {
            s.poll(i);
        }
        assert(s.queued() == 6000);
        assert(s.write((const uint8_t *)b.data(), b.size()) == AsyncServer::OK);
        assert(s.write((const uint8_t *)c.data(), c.size()) == AsyncServer::OK);
        send_limit = 0;
        s.poll(20);
        assert(s.queued() == 8100);
        send_limit = 111;
        while (s.queued()) {
            s.poll(21);
        }
        assert(sent_bytes == a + b + c);
        std::vector<uint8_t> big(ASYNC_SERVER_TX_CAPACITY + 1);
        assert(s.write(big.data(), big.size()) == AsyncServer::QUEUE_FULL);
        AsyncServer::Result e = s.poll(22);
        assert(e.event == AsyncServer::DISCONNECTED && e.error == AsyncServer::QUEUE_FULL);
        assert(s.poll(23).event == AsyncServer::NONE);
    }
    reset();
    {
        AsyncServer s;
        connect_rc = -1;
        connect_error = EINPROGRESS;
        assert(s.start("127.0.0.1", 1, 0, NULL, 0xfffffff0u) == AsyncServer::OK);
        assert(s.poll(0x10).event == AsyncServer::NONE);
        assert(s.poll(0xfffffff0u + 3000u).error == AsyncServer::CONNECT_TIMEOUT);
        assert(s.start("127.0.0.1", 1, 0, NULL, 0) == AsyncServer::OK);
        select_rc = 1;
        socket_error = ECONNREFUSED;
        AsyncServer::Result e = s.poll(1);
        assert(e.event == AsyncServer::DISCONNECTED && e.system_error == ECONNREFUSED);
    }
    reset();
    {
        uint8_t broadcast[4] = { 192, 168, 1, 255 };
        AsyncServer s;
        assert(s.start("auto", 0, 9888, broadcast, 10) == AsyncServer::OK);
        discovery_reply = "GCSERVER1 65536 invalid";
        assert(s.poll(11).event == AsyncServer::NONE);
        discovery_reply = "GCSERVER1 9888 server";
        assert(s.poll(12).event == AsyncServer::CONNECTED);
        recv_mode = 1;
        uint8_t byte;
        assert(s.read(&byte, 1) == -1);
        assert(s.poll(13).error == AsyncServer::PEER_CLOSED);
        assert(s.start("auto", 0, 9888, broadcast, 20) == AsyncServer::OK);
        assert(s.poll(820).error == AsyncServer::DISCOVERY_TIMEOUT);
    }
    reset();
    {
        AsyncServer s;
        assert(s.start("example.test", 1, 0, NULL, 0) == AsyncServer::OK);
        run_dns();
        assert(s.poll(5000).error == AsyncServer::DNS_TIMEOUT);
        assert(s.start("127.0.0.1", 1, 0, NULL, 5001) == AsyncServer::OK);
        dns_result(true);
        assert(s.poll(5001).event == AsyncServer::CONNECTED);
        s.stop();
        assert(s.start("example.test", 1, 0, NULL, 0) == AsyncServer::OK);
        run_dns();
        dns_result(false);
        assert(s.poll(1).error == AsyncServer::DNS_FAILED);
        assert(s.start("example.test", 1, 0, NULL, 0) == AsyncServer::OK);
        void (*old_callback)(void *) = queued_callback;
        void *old_arg = queued_arg;
        s.stop();
        dns_immediate = true;
        assert(s.start("example.test", 1, 0, NULL, 0) == AsyncServer::OK);
        old_callback(old_arg);
        run_dns();
        assert(s.poll(1).event == AsyncServer::CONNECTED);
    }
    reset();
    {
        {
            AsyncServer s;
            assert(s.start("example.test", 1, 0, NULL, 0) == AsyncServer::OK);
            run_dns();
        }
        dns_result(true);
        AsyncServer s;
        dns_immediate = true;
        assert(s.start("example.test", 1, 0, NULL, 0) == AsyncServer::OK);
        run_dns();
        assert(s.poll(1).event == AsyncServer::CONNECTED);
    }
    reset();
    {
        AsyncServer s;
        start_numeric(s);
        std::vector<uint8_t> frame(GC_FRAME_MAX_WIRE, 0x55);
        std::vector<uint8_t> ack(64, 0xaa);
        for (int i = 0; i < 4; ++i) {
            assert(s.write(frame.data(), frame.size()) == AsyncServer::OK);
        }
        for (int i = 0; i < 16; ++i) {
            assert(s.write(ack.data(), ack.size()) == AsyncServer::OK);
        }
        assert(s.queued() == 4 * frame.size() + 16 * ack.size());
        while (s.queued()) {
            s.poll(1);
        }
        assert(sent_bytes.size() == 4 * frame.size() + 16 * ack.size());
    }
    reset();
    {
        AsyncServer s;
        dns_callback_early = true;
        assert(s.start("early.test", 1, 0, NULL, 0) == AsyncServer::OK);
        run_dns();
        assert(s.poll(1).event == AsyncServer::CONNECTED);
    }
    for (int cancel_first = 0; cancel_first < 2; ++cancel_first) {
        reset();
        AsyncServer s;
        assert(s.start("old.test", 1, 0, NULL, 0) == AsyncServer::OK);
        dns_interleaved_server = &s;
        dns_cancel_first = cancel_first != 0;
        run_dns();
        dns_immediate = true;
        run_dns();
        assert(s.poll(1).event == AsyncServer::CONNECTED);
        s.stop();
        assert(s.start("third.test", 1, 0, NULL, 2) == AsyncServer::OK);
        run_dns();
        assert(s.poll(2).event == AsyncServer::CONNECTED);
    }
    return 0;
}
