/*
 * fping.c - fast ping for Windows
 *
 * Design faithfully follows upstream fping (https://github.com/schweikert/fping):
 *   - hosts are pinged in a round-robin fashion, not one after another
 *   - an event queue drives the main loop: each entry is either
 *     "send next ping to host H" or "host H ping #N timed out"
 *   - the main loop blocks in wait_and_process_replies() until either
 *     a reply arrives or the next event is due
 *
 * The Windows port keeps the same control flow; only the socket layer
 * (IcmpSendEcho2 async API) and the time source (QueryPerformanceCounter)
 * differ.
 *
 * Original fping authors: see upstream repository.
 * Windows port: this repository.
 */
#include "winp.h"
#include "fping.h"
#include "optparse.h"
#include "seqmap.h"

#include <stdarg.h>
#include <signal.h>

/* ---- options (defaults match upstream) --------------------------------- */

#define DEFAULT_PING_DATA_SIZE 56
#define DEFAULT_TIMEOUT_MS     500
#define DEFAULT_INTERVAL_MS    25
#define DEFAULT_RETRY          0
#define DEFAULT_COUNT          0 /* 0 = loop mode */

static int   opt_count          = DEFAULT_COUNT;
static int   opt_loop           = 1;
static int64_t opt_timeout_ns   = DEFAULT_TIMEOUT_MS * 1000000LL;
static int64_t opt_interval_ns  = DEFAULT_INTERVAL_MS * 1000000LL;
static int   opt_retry          = DEFAULT_RETRY;
static int   opt_backoff        = 1; /* -B factor */
static int   opt_alive_flag     = 0; /* -a */
static int   opt_unreachable    = 0; /* -u */
static int   opt_quiet          = 0; /* -q */
static int   opt_verbose        = 0; /* -v */
static int   opt_stats          = 0; /* -s */
static int   opt_size           = DEFAULT_PING_DATA_SIZE; /* -b */
static int   opt_ttl            = 0; /* unused on windows API */
static int   opt_show_latency   = 0;
static int   opt_per_recv       = 0; /* -p (period, ms) */
int64_t report_interval_ns = 0; /* -Q */
static int   opt_print_timestamp = 0; /* -D */
static int   opt_name_lookup    = 1;
static int   opt_netdb          = 1;
static int   opt_ipv6           = 0; /* -6 */
static int   opt_random_data    = 0;
static char *opt_src_addr_str   = NULL; /* -S */
static char *opt_filename       = NULL; /* -f */
static int   opt_generate       = 0; /* -g implied */
static int   opt_eliminate      = 0; /* -e (show elapsed) -- default on */

/* ---- globals ------------------------------------------------------------ */

HOST_ENTRY **table = NULL;
int num_hosts = 0;
int max_hostname_len = 0;
int64_t current_time_ns = 0;
int64_t start_time = 0;
int64_t end_time = 0;
int64_t opt_perhost_interval = 0;

static int socket4_opened = 0;
static int socket6_opened = 0;
static volatile int sigint_received = 0;

static struct event_queue ev_queue = { NULL, NULL };

/* ---- small helpers ------------------------------------------------------ */

void update_current_time(void)
{
    current_time_ns = win_now_ns();
}

void crash_and_burn(char *message)
{
    fprintf(stderr, "fping: %s\n", message);
    exit(4);
}

void errno_crash_and_burn(char *message)
{
    fprintf(stderr, "fping: %s: %s\n", message, strerror(errno));
    exit(4);
}

void print_warning(char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "fping: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

static void sig_handler(int sig)
{
    (void)sig;
    sigint_received = 1;
}

/* ---- host table --------------------------------------------------------- */

static int num_alloc = 0;

void add_name(char *name)
{
    struct addrinfo hints, *res, *p;
    int rc;
    int family = opt_ipv6 ? AF_INET6 : AF_INET;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = family;
    hints.ai_socktype = SOCK_RAW;

    rc = getaddrinfo(name, NULL, &hints, &res);
    if (rc != 0) {
        if (!opt_quiet)
            fprintf(stderr, "fping: %s: %s\n", name, gai_strerror(rc));
        num_noaddress++;
        return;
    }

    for (p = res; p != NULL; p = p->ai_next) {
        char hostbuf[NI_MAXHOST];
        if (opt_netdb) {
            rc = getnameinfo(p->ai_addr, (socklen_t)p->ai_addrlen,
                             hostbuf, sizeof(hostbuf), NULL, 0, 0);
            if (rc != 0)
                strcpy(hostbuf, name);
        } else {
            void *addrptr;
            if (p->ai_family == AF_INET)
                addrptr = &((struct sockaddr_in *)p->ai_addr)->sin_addr;
            else
                addrptr = &((struct sockaddr_in6 *)p->ai_addr)->sin6_addr;
            inet_ntop(p->ai_family, addrptr, hostbuf, sizeof(hostbuf));
        }
        add_addr(name, hostbuf, p->ai_addr, (socklen_t)p->ai_addrlen);
        break; /* first address only, like upstream */
    }
    freeaddrinfo(res);
}

void add_addr(char *name, char *host, struct sockaddr *ipaddr, socklen_t ipaddr_len)
{
    HOST_ENTRY *h;

    if (num_hosts >= num_alloc) {
        num_alloc = num_alloc ? num_alloc * 2 : 64;
        table = (HOST_ENTRY **)realloc(table, num_alloc * sizeof(HOST_ENTRY *));
        if (!table)
            crash_and_burn("can't realloc host table");
    }

    h = (HOST_ENTRY *)calloc(1, sizeof(HOST_ENTRY));
    if (!h)
        crash_and_burn("can't allocate host entry");

    h->i = num_hosts;
    h->name = strdup(name);
    h->host = strdup(host);
    memcpy(&h->saddr, ipaddr, ipaddr_len);
    h->saddr_len = ipaddr_len;
    h->timeout = opt_timeout_ns;
    h->min_reply = -1;
    h->min_reply_i = -1;
    h->event_storage_ping = (struct event *)calloc(
        (size_t)(opt_count + opt_retry + 2), sizeof(struct event));
    h->event_storage_timeout = (struct event *)calloc(
        (size_t)(opt_count + opt_retry + 2), sizeof(struct event));
    if (opt_count > 0)
        h->resp_times = (int64_t *)calloc((size_t)(opt_count + 1), sizeof(int64_t));

    if ((int)strlen(host) > max_hostname_len)
        max_hostname_len = (int)strlen(host);

    table[num_hosts++] = h;
}

char *na_cat(char *name, struct in_addr ipaddr)
{
    static char buf[256];
    char a[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &ipaddr, a, sizeof(a));
    snprintf(buf, sizeof(buf), "%s (%s)", name, a);
    return buf;
}

/* ---- CIDR / range generation -------------------------------------------- */

void add_addr_range_ipv4(unsigned long start, unsigned long end)
{
    struct in_addr a;
    unsigned long i;
    char buf[INET_ADDRSTRLEN];
    for (i = start; i <= end; i++) {
        a.s_addr = htonl(i);
        inet_ntop(AF_INET, &a, buf, sizeof(buf));
        add_name(buf);
        if (i == 0xFFFFFFFFUL)
            break;
    }
}

void add_cidr_ipv4(unsigned long ipaddr, unsigned long prefix)
{
    unsigned long mask, base, nr;
    if (prefix > 32) {
        print_warning("invalid prefix %lu", prefix);
        return;
    }
    mask = prefix == 0 ? 0 : 0xFFFFFFFFUL << (32 - prefix);
    base = ipaddr & mask;
    nr = (~mask);
    if (nr < 2) {
        add_addr_range_ipv4(base, base);
    } else {
        add_addr_range_ipv4(base + 1, base + nr - 1);
    }
}

void add_cidr(char *cidr)
{
    char *slash = strchr(cidr, '/');
    char addrbuf[128];
    struct in_addr a;
    unsigned long prefix;

    if (!slash) {
        add_name(cidr);
        return;
    }
    if ((size_t)(slash - cidr) >= sizeof(addrbuf)) {
        print_warning("cidr too long: %s", cidr);
        return;
    }
    memcpy(addrbuf, cidr, (size_t)(slash - cidr));
    addrbuf[slash - cidr] = '\0';
    prefix = strtoul(slash + 1, NULL, 10);
    if (inet_pton(AF_INET, addrbuf, &a) != 1) {
        print_warning("can't parse %s", addrbuf);
        return;
    }
    add_cidr_ipv4(ntohl(a.s_addr), prefix);
}

void add_range(char *start, char *end)
{
    struct in_addr a, b;
    if (inet_pton(AF_INET, start, &a) != 1 || inet_pton(AF_INET, end, &b) != 1) {
        print_warning("can't parse range %s,%s", start, end);
        return;
    }
    add_addr_range_ipv4(ntohl(a.s_addr), ntohl(b.s_addr));
}

/* ---- event queue ---------------------------------------------------------- */

void ev_enqueue(struct event_queue *queue, struct event *event)
{
    struct event *p;

    /* insert sorted by ev_time (ascending).  Most events go near the head. */
    for (p = queue->first; p != NULL && p->ev_time <= event->ev_time; p = p->ev_next)
        ;

    event->ev_prev = p ? p->ev_prev : queue->last;
    event->ev_next = p;
    if (p)
        p->ev_prev = event;
    else
        queue->last = event;
    if (event->ev_prev)
        event->ev_prev->ev_next = event;
    else
        queue->first = event;
}

struct event *ev_dequeue(struct event_queue *queue)
{
    struct event *e = queue->first;
    if (e) {
        queue->first = e->ev_next;
        if (queue->first)
            queue->first->ev_prev = NULL;
        else
            queue->last = NULL;
        e->ev_next = e->ev_prev = NULL;
    }
    return e;
}

void ev_remove(struct event_queue *queue, struct event *event)
{
    if (event->ev_prev)
        event->ev_prev->ev_next = event->ev_next;
    else if (queue->first == event)
        queue->first = event->ev_next;
    if (event->ev_next)
        event->ev_next->ev_prev = event->ev_prev;
    else if (queue->last == event)
        queue->last = event->ev_prev;
    event->ev_next = event->ev_prev = NULL;
}

/* ---- per-host event storage --------------------------------------------- */

void host_add_ping_event(HOST_ENTRY *h, int index, int64_t ev_time)
{
    struct event *e = &h->event_storage_ping[index];
    e->host = h;
    e->ping_index = index;
    e->ev_time = ev_time;
    ev_enqueue(&ev_queue, e);
}

void host_add_timeout_event(HOST_ENTRY *h, int index, int64_t ev_time)
{
    struct event *e = &h->event_storage_timeout[index];
    e->host = h;
    e->ping_index = index;
    e->ev_time = ev_time;
    ev_enqueue(&ev_queue, e);
}

struct event *host_get_timeout_event(HOST_ENTRY *h, int index)
{
    return &h->event_storage_timeout[index];
}

/* ---- statistics ----------------------------------------------------------- */

void stats_reset_interval(HOST_ENTRY *h)
{
    h->num_sent_i = 0;
    h->num_recv_i = 0;
    h->max_reply_i = 0;
    h->min_reply_i = -1;
    h->total_time_i = 0;
}

void stats_add(HOST_ENTRY *h, int index, int success, int64_t latency)
{
    if (success) {
        h->num_recv++;
        h->num_recv_total++;
        h->num_recv_i++;
        if (latency > h->max_reply) h->max_reply = latency;
        if (h->min_reply < 0 || latency < h->min_reply) h->min_reply = latency;
        h->total_time += latency;
        if (latency > h->max_reply_i) h->max_reply_i = latency;
        if (h->min_reply_i < 0 || latency < h->min_reply_i) h->min_reply_i = latency;
        h->total_time_i += latency;
        if (h->resp_times)
            h->resp_times[index] = latency;
        if (latency > max_reply) max_reply = latency;
        if (min_reply < 0 || latency < min_reply) min_reply = latency;
        total_replies++;
        sum_replies += latency;
    }
}

const char *sprint_tm(int64_t t)
{
    static char buf[32];
    if (t < 1000)
        snprintf(buf, sizeof(buf), "%.3g", (double)t);
    else if (t < 1000000)
        snprintf(buf, sizeof(buf), "%.2f", (double)t / 1000.0);
    else
        snprintf(buf, sizeof(buf), "%.2f", (double)t / 1000000.0);
    return buf;
}

/* ---- output --------------------------------------------------------------- */

static void print_host_line(HOST_ENTRY *h, int success, int64_t latency, const char *reply_addr)
{
    if (opt_quiet)
        return;
    if (opt_alive_flag && !success)
        return;
    if (opt_unreachable && success)
        return;
    if (!opt_alive_flag && !opt_unreachable) {
        /* default: show both */
    }

    if (opt_print_timestamp) {
        int64_t sec = win_wallclock_sec();
        printf("[%lld] ", (long long)sec);
    }

    if (success) {
        printf("%-s is alive", h->host);
        if (opt_show_latency || opt_eliminate)
            printf(" (%.2f ms)", (double)latency / 1000000.0);
        if (reply_addr && *reply_addr && strcmp(reply_addr, h->host) != 0)
            printf(" [%s]", reply_addr);
        printf("\n");
    } else {
        printf("%-s is unreachable\n", h->host);
    }
}

void print_perhost_stats(void)
{
    int i;
    printf("\n");
    for (i = 0; i < num_hosts; i++) {
        HOST_ENTRY *h = table[i];
        int sent = h->num_sent;
        int recv = h->num_recv;
        int loss = sent > 0 ? (100 * (sent - recv)) / sent : 0;
        printf("%-*s : xmt/rcv/%%loss = %d/%d/%d%%",
               max_hostname_len, h->name, sent, recv, loss);
        if (recv > 0) {
            int64_t avg = h->total_time / recv;
            printf(", min/avg/max = %.2f/%.2f/%.2f",
                   (double)h->min_reply / 1000000.0,
                   (double)avg / 1000000.0,
                   (double)h->max_reply / 1000000.0);
        }
        printf("\n");
    }
}

void print_global_stats(void)
{
    printf("\n");
    printf("%7d targets\n", num_hosts);
    printf("%7d alive\n", num_alive);
    printf("%7d unreachable\n", num_unreachable);
    printf("%7d unknown addresses\n", num_noaddress);
    printf("\n");
    printf("%7d timeouts (waiting for reply)\n", num_timeout);
    printf("%7d ICMP Echos sent\n", num_pingsent);
    printf("%7d ICMP Echo Replies received\n", num_pingreceived);
    printf("%7d other ICMP received\n", num_othericmprcvd);
    printf("\n");
    if (total_replies > 0) {
        printf(" %.2f ms (min round trip time)\n", (double)min_reply / 1000000.0);
        printf(" %.2f ms (avg round trip time)\n", (double)sum_replies / (double)total_replies / 1000000.0);
        printf(" %.2f ms (max round trip time)\n", (double)max_reply / 1000000.0);
    }
    printf(" %12.3f sec (elapsed real time)\n", (double)(end_time - start_time) / 1000000000.0);
    printf("\n");
}

/* ---- send / receive glue --------------------------------------------------- */

int send_ping(HOST_ENTRY *h, int index)
{
    int rc;
    int seq = h->num_sent;
    (void)index;

    if (h->saddr.ss_family == AF_INET6)
        rc = socket_sendto_ping_ipv6(h, (uint16_t)seq);
    else
        rc = socket_sendto_ping_ipv4(h, (uint16_t)seq);

    if (rc == 0) {
        h->num_sent++;
        h->num_sent_i++;
        num_pingsent++;
        h->last_send_time = current_time_ns;
    }
    return rc;
}

/*
 * handle_reply - called from the socket layer when an echo completes.
 */
/*
 * schedule_next_ping - decide whether host h gets another ping, and if so
 * enqueue the event.  Works for both loop mode and count/retry mode.
 */
static void schedule_next_ping(HOST_ENTRY *h)
{
    int limit;

    if (sigint_received)
        return;

    if (opt_loop) {
        limit = opt_count + opt_retry + 1; /* effectively unbounded storage */
        if (h->num_sent < limit)
            host_add_ping_event(h, h->num_sent, current_time_ns + opt_interval_ns);
        return;
    }

    /* count mode: total allowed sends = count + retry */
    limit = opt_count + opt_retry;
    if (h->num_sent < limit)
        host_add_ping_event(h, h->num_sent, current_time_ns + opt_interval_ns);
}

void handle_reply(HOST_ENTRY *h, int seq_nr, int64_t latency_ns, int success,
                  const char *reply_addr)
{
    struct event *timeout_ev;

    if (success) {
        num_pingreceived++;
        stats_add(h, seq_nr, 1, latency_ns);
        print_host_line(h, 1, latency_ns, reply_addr);
    } else {
        /* error from the echo request itself (e.g. host unreachable) */
        num_othericmprcvd++;
        stats_add(h, seq_nr, 0, 0);
        print_host_line(h, 0, 0, NULL);
    }

    /* cancel the pending timeout event for this ping */
    timeout_ev = host_get_timeout_event(h, seq_nr);
    if (timeout_ev)
        ev_remove(&ev_queue, timeout_ev);

    schedule_next_ping(h);
}

/* ---- main loop ------------------------------------------------------------- */

static void schedule_initial_pings(void)
{
    int i;
    update_current_time();
    start_time = current_time_ns;
    for (i = 0; i < num_hosts; i++) {
        HOST_ENTRY *h = table[i];
        /* stagger initial sends by interval, like upstream round-robin */
        host_add_ping_event(h, 0, current_time_ns + (int64_t)i * opt_interval_ns);
    }
}

void main_loop(void)
{
    update_current_time();

    while (!sigint_received) {
        struct event *ev = ev_queue.first;
        int64_t wait_ns;

        if (!ev) {
            /* no more events and nothing outstanding -> done */
            break;
        }

        wait_ns = ev->ev_time - current_time_ns;
        if (wait_ns < 0)
            wait_ns = 0;

        /* block until a reply arrives or the event is due */
        wait_and_process_replies(wait_ns);
        update_current_time();

        /* process all events that are now due */
        while (ev_queue.first && ev_queue.first->ev_time <= current_time_ns) {
            struct event *e = ev_dequeue(&ev_queue);
            HOST_ENTRY *h = e->host;
            int idx = e->ping_index;

            /* is this a ping event or a timeout event?  Distinguish by
             * which storage array it came from. */
            int is_timeout = (e >= h->event_storage_timeout &&
                              e < h->event_storage_timeout + opt_count + opt_retry + 2);

            if (is_timeout) {
                /* timeout for ping #idx */
                num_timeout++;
                h->num_sent_i++; /* count as sent for interval stats */
                print_host_line(h, 0, 0, NULL);
                stats_add(h, idx, 0, 0);
            } else {
                /* time to send ping #idx */
                int rc = send_ping(h, idx);
                if (rc == 0) {
                    host_add_timeout_event(h, idx,
                        current_time_ns + h->timeout * opt_backoff);
                }
            }
        }
    }
}

/* ---- option parsing --------------------------------------------------------- */

void usage(int rc)
{
    printf("Usage: fping.exe [options] [targets...]\n");
    printf("\n");
    printf("Options:\n");
    printf("   -a         show systems that are alive\n");
    printf("   -A         show targets by address\n");
    printf("   -b n       amount of ping data to send, in bytes (default %d)\n", DEFAULT_PING_DATA_SIZE);
    printf("   -B f       set exponential backoff factor to f\n");
    printf("   -c n       count mode: send n pings to each target\n");
    printf("   -C n       same as -c, report results in verbose format\n");
    printf("   -D         print unix timestamp before each output line\n");
    printf("   -e         show elapsed (round-trip) time on packets\n");
    printf("   -f file    read list of targets from a file ( - means stdin)\n");
    printf("   -g         generate target list from CIDR/netmask or start,end\n");
    printf("              (e.g. fping -g 192.168.1.0/24 or fping -g 192.168.1.1 192.168.1.10)\n");
    printf("   -i n       interval between sending ping packets (in ms) (default %d)\n", DEFAULT_INTERVAL_MS);
    printf("   -l         loop mode: send pings forever\n");
    printf("   -n         show targets by name (reverse DNS)\n");
    printf("   -p n       period between pings to one target (in ms) (in loop/count mode)\n");
    printf("   -q         quiet (don't show per-target/per-ping results)\n");
    printf("   -Q n       same as -q, but show summary every n seconds\n");
    printf("   -r n       number of retries (default %d)\n", DEFAULT_RETRY);
    printf("   -s         print cumulative stats upon exit\n");
    printf("   -S addr    set source address\n");
    printf("   -t n       individual target initial timeout (in ms) (default %d)\n", DEFAULT_TIMEOUT_MS);
    printf("   -u         show systems that are unreachable\n");
    printf("   -v         show version\n");
    printf("   -6         use IPv6 instead of IPv4\n");
    printf("   -h         show this help\n");
    printf("\n");
    exit(rc);
}

static const struct optparse_long longopts[] = {
    { "alive",     'a', 0 },
    { "addr",      'A', 0 },
    { "size",      'b', 1 },
    { "backoff",   'B', 1 },
    { "count",     'c', 1 },
    { "vcount",    'C', 1 },
    { "timestamp", 'D', 0 },
    { "elapsed",   'e', 0 },
    { "file",      'f', 1 },
    { "generate",  'g', 0 },
    { "interval",  'i', 1 },
    { "loop",      'l', 0 },
    { "name",      'n', 0 },
    { "period",    'p', 1 },
    { "quiet",     'q', 0 },
    { "squiet",    'Q', 1 },
    { "retry",     'r', 1 },
    { "stats",     's', 0 },
    { "src",       'S', 1 },
    { "timeout",   't', 1 },
    { "unreach",   'u', 0 },
    { "version",   'v', 0 },
    { "ipv6",      '6', 0 },
    { "help",      'h', 0 },
    { 0, 0, 0 }
};

static void read_targets_from_file(const char *fname)
{
    FILE *fp;
    char line[512];

    if (strcmp(fname, "-") == 0) {
        fp = stdin;
    } else {
        fp = fopen(fname, "r");
        if (!fp) {
            fprintf(stderr, "fping: can't open %s: %s\n", fname, strerror(errno));
            exit(3);
        }
    }

    while (fgets(line, sizeof(line), fp)) {
        char *p = line;
        char *end;
        /* strip leading whitespace */
        while (*p == ' ' || *p == '\t') p++;
        /* strip trailing whitespace/newline */
        end = p + strlen(p);
        while (end > p && (end[-1] == '\n' || end[-1] == '\r' ||
                           end[-1] == ' '  || end[-1] == '\t'))
            *--end = '\0';
        if (*p == '\0' || *p == '#')
            continue;
        if (opt_generate)
            add_cidr(p);
        else
            add_name(p);
    }
    if (fp != stdin)
        fclose(fp);
}

/* ---- finish ----------------------------------------------------------------- */

void finish(void)
{
    int i;

    /* mark alive/unreachable hosts */
    for (i = 0; i < num_hosts; i++) {
        HOST_ENTRY *h = table[i];
        if (h->num_recv > 0)
            num_alive++;
        else
            num_unreachable++;
    }

    update_current_time();
    end_time = current_time_ns;

    /* per-host stats: shown in count mode, or when stats are requested,
     * but not in -a/-u "show only alive/unreachable" scanning mode */
    if ((opt_count > 0 || opt_stats) && !opt_alive_flag && !opt_unreachable)
        print_perhost_stats();
    if (opt_stats)
        print_global_stats();

    fflush(stdout);
}

/* ---- main --------------------------------------------------------------------- */

int main(int argc, char **argv)
{
    struct optparse opts;
    int c;
    int longindex = 0;
    int exit_flag = 0;

    if (win_sock_init() != 0) {
        fprintf(stderr, "fping: WSAStartup failed\n");
        return 4;
    }

    /* defaults */
    opt_eliminate = 1;
    opt_show_latency = 0;

    optparse_init(&opts, argv);
    while ((c = optparse_long(&opts, longopts, &longindex)) != -1) {
        switch (c) {
        case 'a': opt_alive_flag = 1; opt_quiet = 1; break;
        case 'A': opt_netdb = 0; break;
        case 'b': opt_size = atoi(opts.optarg); break;
        case 'B': opt_backoff = atoi(opts.optarg); if (opt_backoff < 1) opt_backoff = 1; break;
        case 'c':
        case 'C':
            opt_count = atoi(opts.optarg);
            opt_loop = 0;
            opt_quiet = 1;
            opt_stats = 1;
            break;
        case 'D': opt_print_timestamp = 1; break;
        case 'e': opt_show_latency = 1; break;
        case 'f': opt_filename = opts.optarg; break;
        case 'g': opt_generate = 1; break;
        case 'i': opt_interval_ns = (int64_t)atoi(opts.optarg) * 1000000LL; break;
        case 'l': opt_loop = 1; break;
        case 'n': opt_name_lookup = 1; break;
        case 'p':
            opt_per_recv = 1;
            opt_perhost_interval = (int64_t)atoi(opts.optarg) * 1000000LL;
            opt_interval_ns = opt_perhost_interval;
            break;
        case 'q': opt_quiet = 1; break;
        case 'Q':
            opt_quiet = 1;
            report_interval_ns = (int64_t)atoi(opts.optarg) * 1000000000LL;
            break;
        case 'r': opt_retry = atoi(opts.optarg); break;
        case 's': opt_stats = 1; break;
        case 'S': opt_src_addr_str = opts.optarg; break;
        case 't': opt_timeout_ns = (int64_t)atoi(opts.optarg) * 1000000LL; break;
        case 'u': opt_unreachable = 1; opt_quiet = 1; break;
        case 'v':
            printf("fping for Windows, version 1.0.0\n");
            printf("based on fping by Roland Schemers et al.\n");
            exit_flag = 1;
            break;
        case '6': opt_ipv6 = 1; break;
        case 'h': usage(0); break;
        case '?':
        default:
            fprintf(stderr, "fping: %s\n", opts.errmsg);
            usage(1);
        }
    }

    if (exit_flag) {
        win_sock_cleanup();
        return 0;
    }

    if (opt_size < 0 || opt_size > 1024)
        opt_size = DEFAULT_PING_DATA_SIZE;

    /* open sockets */
    if (opt_ipv6) {
        if (open_ping_socket_ipv6() != 0) {
            fprintf(stderr, "fping: can't open IPv6 socket (need IPv6 enabled)\n");
            win_sock_cleanup();
            return 4;
        }
        socket6_opened = 1;
        init_ping_buffer_ipv6((size_t)opt_size);
    } else {
        if (open_ping_socket_ipv4() != 0) {
            fprintf(stderr, "fping: can't open ICMP socket\n");
            win_sock_cleanup();
            return 4;
        }
        socket4_opened = 1;
        (void)socket6_opened;
        init_ping_buffer_ipv4((size_t)opt_size);
        if (opt_src_addr_str) {
            struct in_addr sa;
            if (inet_pton(AF_INET, opt_src_addr_str, &sa) != 1) {
                fprintf(stderr, "fping: invalid source address %s\n", opt_src_addr_str);
                win_sock_cleanup();
                return 3;
            }
            socket_set_src_addr_ipv4(&sa);
        }
    }

    /* targets */
    if (opt_filename) {
        read_targets_from_file(opt_filename);
    }

    if (opt_generate) {
        /* -g accepts either:
         *   one argument  : CIDR or single address    (fping -g 192.168.1.0/24)
         *   two arguments : start and end of a range  (fping -g 192.168.1.1 192.168.1.10)
         * The first argument is the current positional; the second (if
         * present and the first is not a CIDR) is the range end. */
        const char *arg = optparse_arg(&opts);
        if (arg) {
            if (strchr(arg, '/')) {
                add_cidr((char *)arg);
            } else {
                const char *next = optparse_arg(&opts);
                if (next)
                    add_range((char *)arg, (char *)next);
                else
                    add_cidr((char *)arg);
            }
        }
    } else {
        const char *arg;
        while ((arg = optparse_arg(&opts)) != NULL)
            add_name((char *)arg);
    }

    if (num_hosts == 0) {
        fprintf(stderr, "fping: no targets specified\n");
        usage(1);
    }

    seqmap_init(opt_timeout_ns * 4);

    signal(SIGINT, sig_handler);

    schedule_initial_pings();
    main_loop();
    finish();

    win_sock_cleanup();

    /* exit code: 0 if all reachable, 1 if some unreachable, 2 if any IP unknown */
    if (num_noaddress > 0)
        return 2;
    if (num_unreachable > 0)
        return 1;
    return 0;
}
