// SPDX-License-Identifier: GPL-2.0-or-later

#include "config.h"
#include <zebra.h> /* strmatch */
#include "lib/command.h"
#include "lib/zlog.h"
#include "hh_dp_config.h"
#include "hh_dp_vty.h"
#include "hh_dp_rpc_stats.h"
#include "hh_dp_comm.h" /* log_dataplane_msg */
#include "hh_dp_vty_common.h"

static void hh_vty_show_version(struct vty *vty) {
    vty_out(vty, "\n Hedgehog-GW plugin %s\n", VER_STRING);
    vty_out(vty, "  Built on %s (%s)\n", BUILD_DATE, BUILD_TYPE);
    vty_out(vty, "   Commit: %s\n", GIT_COMMIT);
    vty_out(vty, "   Branch: %s\n", GIT_BRANCH);
    vty_out(vty, "   Tag   : %s\n\n", strlen(GIT_TAG) ? GIT_TAG : "none");
}

DEFUN (hh_dp_show_plugin_version, hh_dp_show_plugin_version_cmd,
       HH_CMD_SHOW_PLUGIN_VERSION,
       SHOW_STR HH_STR HH_DP_PLUGIN "version\n")
{
    hh_vty_show_version(vty);
    return CMD_SUCCESS;
}

DEFUN (hh_dp_show_rpc_stats, hh_dp_show_rpc_stats_cmd,
       HH_CMD_SHOW_RPC_STATS,
       SHOW_STR HH_STR HH_DP_RPC_STR "show rpc stats\n")
{
    hh_vty_show_stats(vty);
    return CMD_SUCCESS;
}

DEFUN (hh_dp_debug_rpc_msg, hh_dp_debug_rpc_msg_cmd,
       HH_CMD_DEBUG_RPC,
       NO_STR DEBUG_STR HH_STR "RPC messages\n")
{
    log_dataplane_msg = !strmatch(argv[0]->text, "no");
    vty_out(vty, "Hedgehog RPC debugging is now %s\n", log_dataplane_msg ? "enabled" : "disabled");
    zlog_info("Hedgehog RPC debugging is now %s", log_dataplane_msg ? "enabled" : "disabled");
    return CMD_SUCCESS;
}

void hh_dp_vty_init(void)
{
    zlog_info("Initializing HHGW vty commands ...");
    install_element(VIEW_NODE, &hh_dp_show_plugin_version_cmd);
    install_element(VIEW_NODE, &hh_dp_show_rpc_stats_cmd);
    install_element(ENABLE_NODE, &hh_dp_debug_rpc_msg_cmd);
}
