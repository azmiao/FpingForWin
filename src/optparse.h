/* optparse.h -- a re-implementation of optparse.h from upstream fping
 * (public domain / CC0 style, minimal getopt_long replacement)
 *
 * Upstream fping ships its own optparse so that it does not depend on
 * getopt_long being available.  We keep the same interface so the option
 * table in fping.c reads exactly like the upstream one.
 */
#ifndef OPTPARSE_H
#define OPTPARSE_H

struct optparse_long {
    const char *longname;
    int shortname;
    int has_arg; /* 0 = no argument, 1 = required argument */
};

struct optparse {
    char **argv;
    int permute;   /* non-zero to permute (default 1, like GNU getopt) */
    int optind;    /* index of next argv element to process */
    int optopt;    /* option character that caused an error */
    char *optarg;  /* argument to current option (if any) */
    char errmsg[64];
    int subopt;    /* index into the current short-option cluster */
};

#define OPTPARSE_INVALID   -1
#define OPTPARSE_HELP      -2

void optparse_init(struct optparse *options, char **argv);

/* short options: optstring like "abc:d::" (a, b flag; c requires arg) */
int optparse(struct optparse *options, const char *optstring);

/* long options: table terminated by {0,0,0}.  Returns shortname or 0 for
 * long-only options (caller should use an enum >= 256). */
int optparse_long(struct optparse *options,
                  const struct optparse_long *longopts,
                  int *longindex);

const char *optparse_arg(struct optparse *options);

#endif
