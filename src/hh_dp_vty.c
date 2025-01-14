#include "config.h"
#include "lib/command.h"
#include "lib/zlog.h"
#include "hh_dp_config.h"
#include "hh_dp_vty.h"
#include "hh_dp_rpc_stats.h"

#define HH_STR "Hedgehog-GW\n"
#define HH_DP_RPC_STR "RPC stats\n"


DEFUN(hh_dp_show_rpc_stats, hh_dp_show_rpc_stats_cmd,
      "show hedgehog rpc stats",
      SHOW_STR HH_STR HH_DP_RPC_STR "show rpc stats\n")
{
    hh_vty_show_stats(vty);
    return CMD_SUCCESS;
}


void hh_dp_vty_init(void)
{
    zlog_info("Initializing HHGW vty commands ...");
    install_element(VIEW_NODE, &hh_dp_show_rpc_stats_cmd);
}
