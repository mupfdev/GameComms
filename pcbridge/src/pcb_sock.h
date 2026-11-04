/** @file pcb_sock.h
 *
 *  Minimal socket portability layer for the PC bridge.
 *
 *  Copyright (c) 2023-2026, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#ifndef PCB_SOCK_H
#define PCB_SOCK_H

#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define SOCK_INVALID        INVALID_SOCKET
#define sock_close          closesocket
#define SOCK_EAGAIN(e)      ((e) == WSAEWOULDBLOCK)
#define SOCK_EINPROGRESS(e) ((e) == WSAEWOULDBLOCK || (e) == WSAEINPROGRESS)
#define sock_err()          WSAGetLastError()
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
typedef int sock_t;
#define SOCK_INVALID        (-1)
#define sock_close          close
#define SOCK_EAGAIN(e)      ((e) == EAGAIN || (e) == EWOULDBLOCK)
#define SOCK_EINPROGRESS(e) ((e) == EINPROGRESS)
#define sock_err()          errno
#endif

#include <stddef.h>

#endif /* PCB_SOCK_H */
