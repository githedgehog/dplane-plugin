#define _POSIX_C_SOURCE 202501L
#define _GNU_SOURCE

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>

#include "hh_dp_utils.h"
#include "hh_dp_internal.h"

/* delimiter */
#define DELIM " "

/* Max number of opts - should be never exceeded */
#define MAX_NUM_OPTS 64

/* Internal */
#define MAX_ARGS 64
struct plugin_args {
    int argc;
    char *argv[MAX_ARGS];
};

/* free internal stuff alloced for plugin_args */
static void plugin_args_free(struct plugin_args *a)
{
    BUG(!a);
    for(int i = 0; i < MAX_ARGS && a->argc; i++) {
        if (a->argv[i])
            free(a->argv[i]);
        a->argc--;
    }
    memset(a, 0, sizeof(*a));
}

/* push a token into a plugin_arg */
static int plugin_args_push(struct plugin_args *a, const char *token)
{
    BUG(!a || !token, -1);
    if (a->argc >= MAX_ARGS - 1)
        return -1;

    char *tokdup = strdup(token);
    if (!tokdup)
        return -1;
    a->argv[a->argc++] = tokdup;
    return 0;
}

/* initialize plugin_args object */
static int plugin_args_init(struct plugin_args *a, const char *argv0)
{
    BUG(!a , -1);
    memset(a, 0, sizeof(*a));
    return plugin_args_push(a, argv0);
}

/* populate plugin_args object from a string containing args (long / short) */
static int plugin_args_tokenize(struct plugin_args *a, const char *argv0, const char *args_str)
{
    plugin_args_init(a, argv0);

    if (!strlen(args_str))
        return 0;

    /* must dup input str as strtok_x would overwrite its arg */
    char *dup = strdup(args_str);
    if (!dup)
        goto fail;

    char *tok = NULL;
    do {
        char *saveptr;
        tok = strtok_r(a->argc == 1 ? dup : NULL, DELIM, &saveptr);
        if (tok)
            plugin_args_push(a, tok);
    } while (tok && a->argc < MAX_ARGS - 1);

    free(dup);
    return 0;

fail:
    if (dup)
        free(dup);
    if (a)
        plugin_args_free(a);
    return -1;
}

/* Build short opts from long opts */
static void build_short_opts(char *short_opts, const struct option *long_opts)
{
    int c = 0;
    for(const struct option *opt = long_opts; opt->name; opt++) {
        short_opts[c++] = (char)opt->val;
        switch (opt->has_arg) {
            case required_argument:
                short_opts[c++] = ':';
                break;
            case optional_argument:
                short_opts[c++] = ':';
                short_opts[c++] = ':';
                break;
            default:
                break;
        }
    }
    short_opts[c] = '\0';
}

/* find the option in array long_opts having the value 'val'. End of
 * long_opts array is determined by presence of name */
static const struct option *find_opt(int val, const struct option *long_opts) {
    for(const struct option *opt = long_opts; opt->name; opt++) {
        if (opt->val == val)
            return opt;
    }
    return NULL;
}


/* parse plugin options */
static int plugin_getopts(struct plugin_args *a, const struct option *long_opts, parse_opt_cb cb)
{
    BUG(!a || !long_opts || !cb, -1);

    char short_opts [MAX_NUM_OPTS*3 + 1] = {0};
    build_short_opts(short_opts, long_opts);

    int opt;
    do {
        int indexp = INDEXP_UNSET;
        opt = getopt_long(a->argc, a->argv, short_opts, long_opts, &indexp);
        if (opt == EOF) /* Done */
            break;
        if (opt == OPT_UNK) /* unrecognized opt / missing arg */
            return -1;

        /* lookup the option corresponding to opt. If a long opt was supplied, indexp
         * should provide the index. When using short opts, we look up the option from the value opt */
        const struct option *hit_opt = (indexp != INDEXP_UNSET) ? &long_opts[indexp] : find_opt(opt, long_opts);
        if (!hit_opt) {
            zlog_err("No option found matching opt val '%c'", opt);
            return -1;
        }

        /* call callback to process the option */
        if (cb(opt, optarg, indexp, short_opts, long_opts) != 0)
            return -1;

    } while (1);

    return 0;
}

/* main function to parse plugin args */
int plugin_args_parse(const char *argv0, const char *args_str, const struct option *long_opts, parse_opt_cb cb)
{
    optind = 0; /* reset internal state in case of multiple invocations */
    struct plugin_args a;

    /* tokenize args str into (argc, argv[]) in plugin args. Argv0 is needed because
     * getopt_xyz() friends assume that argv[0] is program name. So we must push 'some' name.
     * The value of argv0 will not be processed by getopt, but may be displayed in errors
     * in case a wrong option is passed. Hence caller should specify it so that something meaningful
     * is displayed on error */
    if (plugin_args_tokenize(&a, argv0, args_str) != 0)
        return -1;

    /* process the (argc, argv[]) in plugin_args with getopt and the provided short / long opts.
     * For each valid option, invoke the provided callback 'cb' */
    int r = plugin_getopts(&a, long_opts, cb);

    /* free tokens */
    plugin_args_free(&a);
    return r;
}
