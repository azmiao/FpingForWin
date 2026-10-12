/*
 * socket6_win.c - IPv6 ping socket for Windows, using Icmp6SendEcho2.
 *
 * Same structure as socket4_win.c but for AF_INET6.
 */
#include "winp.h"
#include "fping.h"
#include "icmp_win.h"

#include <iphlpapi.h>
#include <icmpapi.h>

#define MAX6_OUTSTANDING 4096

typedef struct ping6_req {
    int used;
    HOST_ENTRY *host;
    int seq_nr;
    int64_t sent_time_ns;
    HANDLE event;
    char reply_buf[sizeof(ICMPV6_ECHO_REPLY) + 1024];
} PING6_REQ;

static PING6_REQ reqs6[MAX6_OUTSTANDING];

static HANDLE hIcmp6 = NULL;
static char *ping_data_ipv6 = NULL;
static size_t ping_data_size_ipv6 = 0;
static struct sockaddr_in6 opt_src_addr6;

int open_ping_socket_ipv6(void)
{
    int i;
    struct sockaddr_in6 any6;

    memset(&any6, 0, sizeof(any6));
    any6.sin6_family = AF_INET6;
    memset(&opt_src_addr6, 0, sizeof(opt_src_addr6));
    opt_src_addr6.sin6_family = AF_INET6;

    hIcmp6 = Icmp6CreateFile();
    if (hIcmp6 == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "fping: Icmp6CreateFile failed: %lu\n", GetLastError());
        return -1;
    }
    for (i = 0; i < MAX6_OUTSTANDING; i++) {
        reqs6[i].used = 0;
        reqs6[i].event = NULL;
    }
    return 0;
}

void init_ping_buffer_ipv6(size_t ping_data_size)
{
    ping_data_size_ipv6 = ping_data_size;
    ping_data_ipv6 = (char *)calloc(1, ping_data_size ? ping_data_size : 1);
    if (!ping_data_ipv6)
        crash_and_burn("can't malloc ping data (ipv6)");
}

static PING6_REQ *req6_alloc(void)
{
    int i;
    for (i = 0; i < MAX6_OUTSTANDING; i++)
        if (!reqs6[i].used)
            return &reqs6[i];
    return NULL;
}

int socket_sendto_ping_ipv6(HOST_ENTRY *h, uint16_t seq_nr)
{
    PING6_REQ *r = req6_alloc();
    struct sockaddr_in6 *dst = (struct sockaddr_in6 *)&h->saddr;
    DWORD rc;

    if (!r)
        return -1;

    if (!r->event) {
        /* manual-reset event (see socket4_win.c for the rationale) */
        r->event = CreateEvent(NULL, TRUE, FALSE, NULL);
        if (!r->event)
            return -1;
    } else {
        ResetEvent(r->event);
    }

    r->host = h;
    r->seq_nr = seq_nr;
    r->sent_time_ns = current_time_ns;
    r->used = 1;

    /* see socket4_win.c for why the system timeout is 2x the fping timeout */
    {
        DWORD sys_timeout = (DWORD)(fping_timeout_ns / 1000000LL) * 2;
        if (sys_timeout == 0)
            sys_timeout = 1;

        rc = Icmp6SendEcho2(hIcmp6,
                            r->event,
                            NULL,
                            NULL,
                            &opt_src_addr6,
                            dst,
                            ping_data_ipv6,
                            (WORD)ping_data_size_ipv6,
                            NULL,
                            r->reply_buf,
                            sizeof(r->reply_buf),
                            sys_timeout);
    }

    if (rc == 0) {
        DWORD err = GetLastError();
        if (err != ERROR_IO_PENDING) {
            r->used = 0;
            return -1;
        }
    }
    return 0;
}

static void req6_process(PING6_REQ *r)
{
    DWORD n = Icmp6ParseReplies(r->reply_buf, sizeof(r->reply_buf));
    PICMPV6_ECHO_REPLY rep = (PICMPV6_ECHO_REPLY)r->reply_buf;
    int64_t latency = current_time_ns - r->sent_time_ns;
    char addrbuf[INET6_ADDRSTRLEN] = "";
    int success = 0;

    if (n > 0 && rep->Status == IP_SUCCESS) {
        success = 1;
        latency = (int64_t)rep->RoundTripTime * 1000000LL;
        if (latency <= 0)
            latency = 500000;
    }

    {
        struct in6_addr ra;
        memcpy(&ra, &rep->Address, sizeof(ra));
        inet_ntop(AF_INET6, &ra, addrbuf, sizeof(addrbuf));
    }

    r->used = 0;
    handle_reply(r->host, r->seq_nr, latency, success, addrbuf);
}

/*
 * socket6_drain - non-blocking: process every completed IPv6 echo request.
 * Called from the unified wait_and_process_replies() dispatcher.
 * Returns number of replies processed.
 */
int socket6_drain(void)
{
    int i;
    int processed = 0;
    /* manual-reset events: reset explicitly after handling */
    for (i = 0; i < MAX6_OUTSTANDING; i++) {
        if (reqs6[i].used && WaitForSingleObject(reqs6[i].event, 0) == WAIT_OBJECT_0) {
            req6_process(&reqs6[i]);
            ResetEvent(reqs6[i].event);
            processed++;
        }
    }
    return processed;
}

/* expose the event handles of outstanding IPv6 requests so the unified
 * waiter can include them in WaitForMultipleObjects */
int socket6_collect_events(HANDLE *out, int max)
{
    int i;
    int n = 0;
    for (i = 0; i < MAX6_OUTSTANDING && n < max; i++) {
        if (reqs6[i].used)
            out[n++] = reqs6[i].event;
    }
    return n;
}
