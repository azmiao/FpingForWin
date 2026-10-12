/*
 * winp.h - Windows compatibility / portability layer for fping
 *
 * This header centralises everything that upstream fping gets from POSIX
 * headers but which is missing or different under Winsock / MinGW.
 */
#ifndef _WINP_H
#define _WINP_H

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600 /* Vista+ : needed for inet_ntop, IcmpSendEcho2 IPv6 */
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* ---- socket helpers ---------------------------------------------------- */

typedef int socklen_t;

static __inline int win_sock_init(void)
{
    WSADATA wsa;
    return WSAStartup(MAKEWORD(2, 2), &wsa);
}

static __inline void win_sock_cleanup(void)
{
    WSACleanup();
}

/* ---- high resolution time ---------------------------------------------- */
/* current time in nanoseconds, from a monotonic source                     */

static __inline int64_t win_now_ns(void)
{
    static LARGE_INTEGER freq;
    static int initialised = 0;
    LARGE_INTEGER counter;

    if (!initialised) {
        QueryPerformanceFrequency(&freq);
        initialised = 1;
    }
    QueryPerformanceCounter(&counter);
    return (int64_t)((counter.QuadPart * 1000000000LL) / freq.QuadPart);
}

/* wall clock seconds (for -D timestamp) */
static __inline int64_t win_wallclock_sec(void)
{
    return (int64_t)time(NULL);
}

#endif /* _WINP_H */
