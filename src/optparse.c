/* optparse.c -- portable getopt_long replacement
 *
 * This is a compact implementation modelled on the public-domain optparse
 * used by upstream fping.  Only the features fping needs are provided:
 *  - short option clusters ("-abc")
 *  - required arguments ("-c 3" or "-c3")
 *  - long options ("--count=3" / "--count 3")
 *  - "--" end-of-options marker
 *  - "-" treated as a non-option argument (fping uses it for stdin)
 */
#include "optparse.h"

#include <stdio.h>
#include <string.h>

#define OPTPARSE_MSG_INVALID "invalid option"
#define OPTPARSE_MSG_MISSING "option requires an argument"
#define OPTPARSE_MSG_TOOMANY "option takes no arguments"

static int optparse_error(struct optparse *options, const char *msg, const char *data)
{
    unsigned p = 0;
    const char *sep = " -- '";
    while (*msg)
        options->errmsg[p++] = *msg++;
    while (*sep)
        options->errmsg[p++] = *sep++;
    while (p < sizeof(options->errmsg) - 1 && *data)
        options->errmsg[p++] = *data++;
    options->errmsg[p] = '\0';
    return '?';
}

void optparse_init(struct optparse *options, char **argv)
{
    options->argv = argv;
    options->permute = 1;
    options->optind = 1;
    options->subopt = 0;
    options->optarg = 0;
    options->errmsg[0] = '\0';
}

static int optparse_is_dashdash(const char *arg)
{
    return arg != 0 && arg[0] == '-' && arg[1] == '-' && arg[2] == '\0';
}

static int optparse_is_shortopt(const char *arg)
{
    return arg != 0 && arg[0] == '-' && arg[1] != '-' && arg[1] != '\0';
}

static int optparse_is_longopt(const char *arg)
{
    return arg != 0 && arg[0] == '-' && arg[1] == '-' && arg[2] != '\0';
}

static void optparse_permute(struct optparse *options, int index)
{
    char *nonoption = options->argv[index];
    int i;
    for (i = index; i < options->optind - 1; i++)
        options->argv[i] = options->argv[i + 1];
    options->argv[options->optind - 1] = nonoption;
}

static int optparse_argtype(const char *optstring, char c)
{
    int i;
    if (c == ':')
        return -1;
    for (i = 0; optstring[i]; i++) {
        if (optstring[i] == ':')
            continue; /* ':' is an argument marker, never an option char */
        if (optstring[i] == c) {
            /* option found; count ':' that follow */
            if (optstring[i + 1] != ':')
                return 0; /* no argument */
            if (optstring[i + 2] == ':')
                return 2; /* optional argument */
            return 1;     /* required argument */
        }
    }
    return -1;
}

int optparse(struct optparse *options, const char *optstring)
{
    int type;
    char *next;
    char *option = options->argv[options->optind];
    options->errmsg[0] = '\0';
    options->optopt = 0;
    options->optarg = 0;
    if (option == 0) {
        return -1;
    } else if (optparse_is_dashdash(option)) {
        options->optind++; /* consume "--" */
        return -1;
    } else if (!optparse_is_shortopt(option)) {
        if (options->permute) {
            int index = options->optind++;
            int r = optparse(options, optstring);
            optparse_permute(options, index);
            options->optind--;
            return r;
        } else {
            return -1;
        }
    }
    option += options->subopt + 1;
    options->optopt = option[0];
    type = optparse_argtype(optstring, option[0]);
    next = options->argv[options->optind + 1];
    switch (type) {
    case -1:
        options->optind++;
        return optparse_error(options, OPTPARSE_MSG_INVALID, option);
    case 0:
        if (option[1]) {
            options->subopt++;
        } else {
            options->subopt = 0;
            options->optind++;
        }
        return option[0];
    case 1:
        if (option[1]) {
            options->optarg = option + 1;
            options->optind++;
        } else if (next != 0 && !(next[0] == '-' && next[1] != '\0'
                                  && strchr(optstring, ':') == 0)) {
            /* take next argv as argument unless it looks like an option and
             * the optstring doesn't use ':' (paranoia; upstream takes any) */
            options->optarg = next;
            options->optind += 2;
        } else if (next != 0) {
            options->optarg = next;
            options->optind += 2;
        } else {
            options->subopt = 0;
            options->optind++;
            return optparse_error(options, OPTPARSE_MSG_MISSING, option);
        }
        options->subopt = 0;
        return option[0];
    default:
        options->optind++;
        return '?';
    }
}

const char *optparse_arg(struct optparse *options)
{
    char *option = options->argv[options->optind];
    options->subopt = 0;
    if (option != 0)
        options->optind++;
    return option;
}

static int optparse_longopts_end(const struct optparse_long *longopts, int i)
{
    return !longopts[i].longname && !longopts[i].shortname;
}

static void optparse_from_long(const struct optparse_long *longopts, char *optstring)
{
    char *p = optstring;
    int i;
    for (i = 0; !optparse_longopts_end(longopts, i); i++) {
        if (longopts[i].shortname && longopts[i].shortname < 127) {
            int a;
            *p++ = (char)longopts[i].shortname;
            for (a = 0; a < longopts[i].has_arg; a++)
                *p++ = ':';
        }
    }
    *p = '\0';
}

int optparse_long(struct optparse *options,
                  const struct optparse_long *longopts,
                  int *longindex)
{
    char *option = options->argv[options->optind];
    if (option == 0) {
        return -1;
    } else if (optparse_is_dashdash(option)) {
        options->optind++;
        return -1;
    } else if (optparse_is_shortopt(option)) {
        char optstring[128];
        optparse_from_long(longopts, optstring);
        return optparse(options, optstring);
    } else if (optparse_is_longopt(option)) {
        /* long option: match against the table */
        char *name = option + 2;
        char *equals = strchr(name, '=');
        int namelen = equals ? (int)(equals - name) : (int)strlen(name);
        int i;
        int matched = -1;
        int ambiguous = 0;
        for (i = 0; !optparse_longopts_end(longopts, i); i++) {
            const char *ln = longopts[i].longname;
            if (!ln)
                continue;
            if (strncmp(ln, name, (size_t)namelen) == 0) {
                if ((int)strlen(ln) == namelen) {
                    matched = i; /* exact match */
                    ambiguous = 0;
                    break;
                }
                if (matched >= 0)
                    ambiguous = 1;
                else
                    matched = i;
            }
        }
        if (matched < 0 || ambiguous) {
            options->optind++;
            return optparse_error(options, OPTPARSE_MSG_INVALID, name);
        }
        /* found */
        options->optopt = longopts[matched].shortname;
        if (longopts[matched].has_arg) {
            if (equals) {
                options->optarg = equals + 1;
            } else if (options->argv[options->optind + 1]) {
                options->optarg = options->argv[options->optind + 1];
                options->optind++;
            } else {
                options->optind++;
                return optparse_error(options, OPTPARSE_MSG_MISSING, name);
            }
        } else if (equals) {
            options->optind++;
            return optparse_error(options, OPTPARSE_MSG_TOOMANY, name);
        }
        options->optind++;
        if (longindex)
            *longindex = matched;
        return longopts[matched].shortname ? longopts[matched].shortname : 0;
    } else {
        return -1;
    }
}
