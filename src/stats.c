/* stats.c - global statistics counters for fping (identical to upstream
 * defaults).  All counters live here so fping.c and output code can share
 * them. */
#include "winp.h"
#include "fping.h"

int num_alive = 0;
int num_unreachable = 0;
int num_noaddress = 0;
int num_timeout = 0;
int num_pingsent = 0;
int num_pingreceived = 0;
int num_othericmprcvd = 0;
int64_t max_reply = 0;
int64_t min_reply = -1;
int64_t total_replies = 0;
int64_t sum_replies = 0;

void stats_init(void)
{
    num_alive = num_unreachable = num_noaddress = num_timeout = 0;
    num_pingsent = num_pingreceived = num_othericmprcvd = 0;
    max_reply = 0;
    min_reply = -1;
    total_replies = 0;
    sum_replies = 0;
}
