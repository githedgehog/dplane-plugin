#ifndef SRC_HH_DP_UTILS_H_
#define SRC_HH_DP_UTILS_H_

#include <getopt.h>

#define INDEXP_UNSET (-1)
#define OPT_UNK ((int)'?')

/*
 * Callback signature to process an option in a specific manner. This callback should
 * return 0 on success and != 0 otherwise. The callback should not take any ownership on them.
 *
 *   Opt:
 *       the value/code of the short/long option matched
 *   opt_arg:
 *       in options that can have/require values, the value of optarg.
 *   long_opt:
 *       A pointer to the option within the long_opts array of struct options provided to
 *       plugin_args_parse() that the value of Opt corresponds to.
 */
typedef int (*parse_opt_cb)(
        int opt,
        const char *opt_arg,
        const struct option *long_opt
);


/* Parse the options passed as a single string in args_str according to the specified short options and long options.
 * For each valid option encountered, the callback parse_opt_cb will be invoked.
 */
int plugin_args_parse(const char *argv0, const char *args_str, const struct option *long_opts, parse_opt_cb cb);


#endif /* SRC_HH_DP_UTILS_H_ */
