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
 *       the value/code of the short/long option matched or OPT_UNK
 *   opt_arg:
 *       in options that can have/require values, the value of optarg.
 *   indexp:
 *       in long options, the index in array long_opts for the option the callback is invoked for.
 *       indexp is only valid if distinct from INDEXP_UNSET. When using short options, indexp always
 *       equals INDEXP_UNSET.
 *
 *   short_opts:
 *       The string of short options that was provided to plugin_args_parse(). See below.
 *   long_opts:
 *       The array of struct option passed when invoking plugin_args_parse(). See below.
 */
typedef int (*parse_opt_cb)(
        int opt,
        const char *opt_arg,
        int indexp,
        const char *short_opts,
        const struct option *long_opts
);


/* Parse the options passed as a single string in args_str according to the specified short options and long options.
 * For each valid option encountered, the callback parse_opt_cb will be invoked.
 */
int plugin_args_parse(const char *argv0, const char *args_str, const struct option *long_opts, parse_opt_cb cb);


#endif /* SRC_HH_DP_UTILS_H_ */
