#ifndef SRC_HH_DP_PROCESS_H_
#define SRC_HH_DP_PROCESS_H_

#include "zebra/zebra_dplane.h"

extern struct zebra_dplane_provider *prov_p;

void zd_hh_process_update(struct zebra_dplane_provider *prov, struct zebra_dplane_ctx *ctx);

#endif
