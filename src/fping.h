#ifndef _FPING_H
#define _FPING_H

#include "winp.h"

#include <stdint.h>
#include <inttypes.h>
#include <time.h>

/* ---- structure definitions --------------------------------------------- */

typedef struct host_entry {
    int i;                          /* index into array */
    char *name;                     /* name as given by user */
    char *host;                     /* text description of host */
    struct sockaddr_storage saddr;  /* target address */
    socklen_t saddr_len;
    int64_t timeout;                /* per-host timeout (ns) */
    int64_t last_send_time;         /* time of last packet sent */
    int num_sent;
    int num_recv;
    int num_recv_total;
    int64_t max_reply;
    int64_t min_reply;
    int64_t total_time;
    /* per-interval (reset on every report) */
    int num_sent_i;
    int num_recv_i;
    int64_t max_reply_i;
    int64_t min_reply_i;
    int64_t total_time_i;
    int64_t *resp_times;            /* individual response times */

    struct event *event_storage_ping;
    struct event *event_storage_timeout;

    /* windows async completion: one OVERLAPPED/event per outstanding ping */
    void *win_ping_handle;          /* IcmpHandle used for this host */

    int ring_size;                  /* size of the per-host event ring */
} HOST_ENTRY;

struct event {
    HOST_ENTRY *host;
    int ping_index;
    int64_t ev_time;
    int in_queue;   /* 1 while this event sits in the queue */
    int is_timeout; /* 1 = timeout event, 0 = send-ping event */
    struct event *ev_next;
    struct event *ev_prev;
};

struct event_queue {
    struct event *first;
    struct event *last;
};

/* ---- global variables --------------------------------------------------- */

extern HOST_ENTRY **table;
extern int num_hosts;
extern int max_hostname_len;
extern int64_t current_time_ns;
extern int64_t start_time;
extern int64_t end_time;
extern int64_t report_interval_ns;

/* stats globals (defined in stats.c) */
extern int num_alive, num_unreachable, num_noaddress, num_timeout;
extern int num_pingsent, num_pingreceived, num_othericmprcvd;
extern int64_t max_reply, min_reply, total_replies, sum_replies;

/* ---- debug macro -------------------------------------------------------- */

#if defined(DEBUG) || defined(_DEBUG)
extern int opt_debug_trace_on;
#define dbg_printf(fmt, ...) do { if (opt_debug_trace_on) { \
    fprintf(stderr, "[%10.5f] ", (double)(current_time_ns / 1000) / 1000000); \
    fprintf(stderr, fmt, __VA_ARGS__); } } while (0)
#else
#define dbg_printf(fmt, ...)
#endif

/* ---- fping.c ------------------------------------------------------------ */

void add_name(char *name);
extern int64_t fping_timeout_ns; /* per-target timeout shared with socket layer */
void add_addr(char *name, char *host, struct sockaddr *ipaddr, socklen_t ipaddr_len);
char *na_cat(char *name, struct in_addr ipaddr);
int send_ping(HOST_ENTRY *h, int index);
void usage(int);
int wait_for_reply(int64_t wait_time);
void stats_reset_interval(HOST_ENTRY *h);
void main_loop(void);
void finish(void);
const char *sprint_tm(int64_t t);
void ev_enqueue(struct event_queue *queue, struct event *event);
struct event *ev_dequeue(struct event_queue *queue);
void ev_remove(struct event_queue *queue, struct event *event);
void add_cidr(char *cidr);
void add_cidr_ipv4(unsigned long ipaddr, unsigned long prefix);
void add_range(char *start, char *end);
void add_addr_range_ipv4(unsigned long start, unsigned long end);
void print_warning(char *fmt, ...);
int addr_cmp(struct sockaddr *a, struct sockaddr *b);
void host_add_ping_event(HOST_ENTRY *h, int index, int64_t ev_time);
void host_add_timeout_event(HOST_ENTRY *h, int index, int64_t ev_time);
struct event *host_get_timeout_event(HOST_ENTRY *h, int index);
void stats_add(HOST_ENTRY *h, int index, int success, int64_t latency);
void update_current_time(void);
void print_perhost_stats(void);
void print_global_stats(void);
void print_interval_stats(void);
void crash_and_burn(char *message);
void errno_crash_and_burn(char *message);

/* ---- socket layer (platform) -------------------------------------------- */

int  open_ping_socket_ipv4(void);
void init_ping_buffer_ipv4(size_t ping_data_size);
void socket_set_src_addr_ipv4(struct in_addr *src_addr);
int  socket_sendto_ping_ipv4(HOST_ENTRY *h, uint16_t seq_nr);
int  wait_and_process_replies(int64_t timeout_ns);

int  open_ping_socket_ipv6(void);
void init_ping_buffer_ipv6(size_t ping_data_size);
int  socket_sendto_ping_ipv6(HOST_ENTRY *h, uint16_t seq_nr);
int  socket6_drain(void);
int  socket6_collect_events(HANDLE *out, int max);

/* callback invoked by socket layer when a reply (or error) arrives */
void handle_reply(HOST_ENTRY *h, int seq_nr, int64_t latency_ns, int success,
                  const char *reply_addr);

#endif /* _FPING_H */
