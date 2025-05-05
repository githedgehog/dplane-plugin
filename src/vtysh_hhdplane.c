// SPDX-License-Identifier: GPL-2.0-or-later

#include "config.h" /* FRR's */
#include <lib/command.h>

#include "hh_dp_vty_common.h"

extern int vtysh_client_execute_name(const char *name, const char *line);
extern int show_one_daemon(struct vty *vty, struct cmd_token **argv, int argc, const char *name);

DEFUN (vtysh_show_hedgehog_plugin_version,
       vtysh_show_hedgehog_plugin_version_cmd,
       HH_CMD_SHOW_PLUGIN_VERSION,
       SHOW_STR HH_STR HH_DP_PLUGIN "version\n")
{
    vtysh_client_execute_name("zebra", self->string);
    return CMD_SUCCESS;
}

DEFUN (vtysh_show_hedgehog_rpc_stats,
       vtysh_show_hedgehog_rpc_stats_cmd,
       HH_CMD_SHOW_RPC_STATS,
       SHOW_STR HH_STR HH_DP_RPC_STR "show rpc stats\n")
{
    vtysh_client_execute_name("zebra", self->string);
    return CMD_SUCCESS;
}

DEFUN (vtysh_debug_hh_rpc_msg, vtysh_debug_hh_rpc_msg_cmd,
       HH_CMD_DEBUG_RPC,
       NO_STR DEBUG_STR HH_STR "RPC messages\n")
{
    return show_one_daemon(vty, argv, argc, "zebra");
}

int vtysh_extension(void)
{
    install_element(VIEW_NODE, &vtysh_show_hedgehog_rpc_stats_cmd);
    install_element(VIEW_NODE, &vtysh_show_hedgehog_plugin_version_cmd);
    install_element(ENABLE_NODE, &vtysh_debug_hh_rpc_msg_cmd);
    return 0;
}


