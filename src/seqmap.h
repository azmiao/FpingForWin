/*
 * seqmap.h - mapping between sequence number and (host, ping_nr)
 * Identical logic to upstream fping src/seqmap.h
 */
#ifndef _SEQMAP_H
#define _SEQMAP_H

#include <stdint.h>

#define SEQMAP_MAXSEQ 65536

typedef struct seqmap_value {
    unsigned int host_nr;
    unsigned int ping_count;
    int64_t ping_ts;
} SEQMAP_VALUE;

void seqmap_init(int64_t timeout);
unsigned int seqmap_add(unsigned int host_nr, unsigned int ping_count, int64_t timestamp);
SEQMAP_VALUE *seqmap_fetch(unsigned int id, int64_t now);

#endif
