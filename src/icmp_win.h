/*
 * icmp_win.h - ICMP helpers for Windows
 *
 * On Windows the ICMP echo request is constructed by the OS (via
 * IcmpSendEcho2 / Icmp6SendEcho2), so unlike upstream fping we do NOT need
 * to lay out struct icmp by hand.  This header only provides the internet
 * checksum helper (kept for parity with upstream calcsum) and avoids any
 * macro names that collide with <icmpapi.h> / <ws2tcpip.h>.
 */
#ifndef _ICMP_WIN_H
#define _ICMP_WIN_H

#include <stdint.h>

/* internet checksum (same algorithm as upstream calcsum) */
static __inline unsigned short fping_in_cksum(unsigned short *buf, int len)
{
    unsigned long sum = 0;
    while (len > 1) {
        sum += *buf++;
        len -= 2;
    }
    if (len == 1)
        sum += *(unsigned char *)buf;
    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    return (unsigned short)~sum;
}

#endif /* _ICMP_WIN_H */
