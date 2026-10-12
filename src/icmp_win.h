/*
 * icmp_win.h - minimal ICMP/ICMPv6 header structures for Windows
 *
 * Winsock does not ship <netinet/ip_icmp.h>, so we define just the parts
 * that fping needs.  Layout follows RFC 792 / RFC 4443.
 */
#ifndef _ICMP_WIN_H
#define _ICMP_WIN_H

#include <stdint.h>

#define ICMP_ECHOREPLY      0
#define ICMP_ECHO           8
#define ICMP_TSTAMP         13
#define ICMP_TSTAMPREPLY    14
#define ICMP_MINLEN         8

/* ICMPv6 types */
#define ICMP6_ECHO_REQUEST  128
#define ICMP6_ECHO_REPLY    129
#define ICMP6_MINLEN        8

#pragma pack(push, 1)

struct icmp_echo_hdr {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t seq;
};

struct icmp_tstamp_hdr {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t seq;
    uint32_t otime; /* originate timestamp (ms since midnight UTC) */
    uint32_t rtime; /* receive timestamp   */
    uint32_t ttime; /* transmit timestamp  */
};

#pragma pack(pop)

/* internet checksum (same algorithm as upstream calcsum) */
static __inline unsigned short in_cksum_hdr(unsigned short *buf, int len)
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
