/*
 * socket4_win.c - IPv4 ping socket for Windows, using IcmpSendEcho2.
 *
 * Upstream fping uses a raw socket + select().  On Windows we use the
 * IP Helper API (ICMP.DLL) which:
 *   - does not require administrator privileges
 *   - supports fully asynchronous operation (event object per request)
 *
 * The asynchronous model maps naturally onto fping's event-queue main
 * loop: every outstanding ping gets an event HANDLE; wait_and_process
 * _replies() does WaitForMultipleObjects() until the next fping event
 * is due, then drains all completed echo requests and feeds them back
 * through handle_reply().
 */
#include "winp.h"
#include "fping.h"
#include "icmp_win.h"
#include "seqmap.h"

#include <iphlpapi.h>
#include <icmpapi.h>

/* -- outstanding ping bookkeeping --------------------------------------- */

#define MAX_OUTSTANDING 4096

typedef struct ping_req {
    int used;
    HOST_ENTRY *host;
    int seq_nr;
    int64_t sent_time_ns;
    HANDLE event;
    char reply_buf[sizeof(ICMP_ECHO_REPLY) + 1024];
} PING_REQ;

static PING_REQ reqs[MAX_OUTSTANDING];
static int req_count = 0;

static HANDLE hIcmp = NULL;
static char *ping_data_ipv4 = NULL;
static size_t ping_data_size_ipv4 = 0;
static IPAddr opt_src_addr_ipv4 = 0; /* INADDR_ANY */

/* -- public API ---------------------------------------------------------- */

int open_ping_socket_ipv4(void)
{
    int i;
    hIcmp = IcmpCreateFile();
    if (hIcmp == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "fping: IcmpCreateFile failed: %lu\n", GetLastError());
        return -1;
    }
    for (i = 0; i < MAX_OUTSTANDING; i++) {
        reqs[i].used = 0;
        reqs[i].event = NULL;
    }
    return 0;
}

void init_ping_buffer_ipv4(size_t ping_data_size)
{
    ping_data_size_ipv4 = ping_data_size;
    ping_data_ipv4 = (char *)calloc(1, ping_data_size ? ping_data_size : 1);
    if (!ping_data_ipv4)
        crash_and_burn("can't malloc ping data");
}

void socket_set_src_addr_ipv4(struct in_addr *src_addr)
{
    opt_src_addr_ipv4 = src_addr->s_addr;
}

static PING_REQ *req_alloc(void)
{
    int i;
    for (i = 0; i < MAX_OUTSTANDING; i++) {
        if (!reqs[i].used)
            return &reqs[i];
    }
    return NULL;
}

/*
 * socket_sendto_ping_ipv4 - fire one async echo request for host h.
 * seq_nr is kept only so the event-queue bookkeeping matches upstream;
 * the actual ICMP seq is chosen by the OS.
 */
int socket_sendto_ping_ipv4(HOST_ENTRY *h, uint16_t seq_nr)
{
    PING_REQ *r = req_alloc();
    struct sockaddr_in *sin = (struct sockaddr_in *)&h->saddr;
    DWORD rc;

    if (!r) {
        /* too many outstanding requests; treat as send failure */
        return -1;
    }

    if (!r->event) {
        r->event = CreateEvent(NULL, FALSE, FALSE, NULL);
        if (!r->event)
            return -1;
    } else {
        ResetEvent(r->event);
    }

    r->host = h;
    r->seq_nr = seq_nr;
    r->sent_time_ns = current_time_ns;
    r->used = 1;

    rc = IcmpSendEcho2(hIcmp,
                       r->event,
                       NULL,     /* no APC */
                       NULL,     /* no APC context */
                       opt_src_addr_ipv4,
                       sin->sin_addr.s_addr,
                       ping_data_ipv4,
                       (WORD)ping_data_size_ipv4,
                       NULL,     /* default IP options */
                       r->reply_buf,
                       sizeof(r->reply_buf),
                       1000);    /* per-call timeout (we do our own timing) */

    if (rc == 0) {
        DWORD err = GetLastError();
        if (err != ERROR_IO_PENDING) {
            /* immediate failure */
            r->used = 0;
            return -1;
        }
    }
    /* else: completed synchronously; event is signalled anyway */

    req_count++;
    return 0;
}

/* -- receive side --------------------------------------------------------- */

static void req_process(PING_REQ *r)
{
    DWORD n = IcmpParseReplies(r->reply_buf, sizeof(r->reply_buf));
    PICMP_ECHO_REPLY rep = (PICMP_ECHO_REPLY)r->reply_buf;
    int64_t latency = current_time_ns - r->sent_time_ns;
    char addrbuf[INET_ADDRSTRLEN] = "";
    int success = 0;

    if (n > 0 && rep->Status == IP_SUCCESS) {
        success = 1;
        /* RoundTripTime is in ms; if 0 it means < 1ms */
        latency = (int64_t)rep->RoundTripTime * 1000000LL;
        if (latency <= 0)
            latency = 500000; /* 0.5 ms */
    }

    {
        struct in_addr ra;
        ra.s_addr = rep->Address;
        inet_ntop(AF_INET, &ra, addrbuf, sizeof(addrbuf));
    }

    r->used = 0;
    req_count--;

    handle_reply(r->host, r->seq_nr, latency, success, addrbuf);
}

/*
 * wait_and_process_replies - block until at least one echo request
 * completes OR timeout_ns elapses, then process every completed request.
 * Returns the number of replies processed (0 = timeout expired).
 */
int wait_and_process_replies(int64_t timeout_ns)
{
    HANDLE handles[2 * MAXIMUM_WAIT_OBJECTS];
    int n = 0;
    int i;
    DWORD wait_ms;
    int processed = 0;

    /* collect IPv4 outstanding events */
    for (i = 0; i < MAX_OUTSTANDING && n < MAXIMUM_WAIT_OBJECTS; i++) {
        if (reqs[i].used) {
            handles[n] = reqs[i].event;
            n++;
        }
    }

    /* collect IPv6 outstanding events (if any) */
    n += socket6_collect_events(&handles[n], MAXIMUM_WAIT_OBJECTS);

    if (n == 0) {
        /* nothing outstanding: just sleep until the next event is due */
        if (timeout_ns > 0)
            Sleep((DWORD)(timeout_ns / 1000000LL));
        update_current_time();
        return 0;
    }

    wait_ms = (DWORD)((timeout_ns > 0 ? timeout_ns : 0) / 1000000LL);
    if (wait_ms == 0 && timeout_ns > 0)
        wait_ms = 1;

    WaitForMultipleObjects(n, handles, FALSE, wait_ms);
    update_current_time();

    /* drain all signalled IPv4 requests */
    for (i = 0; i < MAX_OUTSTANDING; i++) {
        if (reqs[i].used && WaitForSingleObject(reqs[i].event, 0) == WAIT_OBJECT_0) {
            req_process(&reqs[i]);
            processed++;
        }
    }

    /* drain all signalled IPv6 requests */
    processed += socket6_drain();

    return processed;
}
