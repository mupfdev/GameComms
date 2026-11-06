#ifndef ASYNC_SERVER_TEST_PLATFORM_H
#define ASYNC_SERVER_TEST_PLATFORM_H
#include <stddef.h>
#include <stdint.h>

#define AF_INET      2
#define SOCK_STREAM  1
#define SOCK_DGRAM   2
#define IPPROTO_TCP  6
#define TCP_NODELAY  1
#define SOL_SOCKET   1
#define SO_BROADCAST 2
#define SO_ERROR     4
#define F_GETFL      3
#define F_SETFL      4
#define O_NONBLOCK   8
#define FD_ZERO(x)   ((x)->bits = 0)
#define FD_SET(n, x) ((x)->bits |= 1)
#define fd_set       fake_fd_set
#define timeval      fake_timeval
struct fake_fd_set
{
    int bits;
};
struct fake_timeval
{
    long tv_sec, tv_usec;
};
typedef unsigned int socklen_t;
struct in_addr
{
    uint32_t s_addr;
};
struct sockaddr
{
    unsigned short sa_family;
    char data[14];
};
struct sockaddr_in
{
    unsigned short sin_family, sin_port;
    in_addr sin_addr;
    char padding[8];
};
#define socket      fake_socket
#define closesocket fake_closesocket
#define fcntl       fake_fcntl
#define setsockopt  fake_setsockopt
#define getsockopt  fake_getsockopt
#define connect     fake_connect
#define sendto      fake_sendto
#define recvfrom    fake_recvfrom
#define send        fake_send
#define recv        fake_recv
#define select      fake_select
#define inet_pton   fake_inet_pton
#define htons       fake_htons
int fake_socket(int, int, int);
int fake_closesocket(int);
int fake_fcntl(int, int, int);
int fake_setsockopt(int, int, int, const void *, socklen_t);
int fake_getsockopt(int, int, int, void *, socklen_t *);
int fake_connect(int, const sockaddr *, socklen_t);
int fake_sendto(int, const void *, size_t, int, const sockaddr *, socklen_t);
int fake_recvfrom(int, void *, size_t, int, sockaddr *, socklen_t *);
int fake_send(int, const void *, size_t, int);
int fake_recv(int, void *, size_t, int);
int fake_select(int, fake_fd_set *, fake_fd_set *, fake_fd_set *, fake_timeval *);
int fake_inet_pton(int, const char *, void *);
uint16_t fake_htons(uint16_t);

struct ip_addr_t
{
    uint32_t address;
};
typedef int err_t;
#define ERR_OK                 0
#define ERR_INPROGRESS         -5
#define LWIP_DNS_ADDRTYPE_IPV4 0
#define IP_IS_V4(a)            ((a) != 0)
#define ip_addr_get_ip4_u32(a) ((a)->address)
typedef void (*dns_found_callback)(const char *, const ip_addr_t *, void *);
err_t dns_gethostbyname_addrtype(const char *, ip_addr_t *, dns_found_callback, void *, int);
err_t tcpip_try_callback(void (*)(void *), void *);
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x)        ((void)(x))
#define portEXIT_CRITICAL(x)         ((void)(x))
#endif
