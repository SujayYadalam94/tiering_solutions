#pragma once

#include <stdint.h>
#include "arms.h"

#ifdef __cplusplus
extern "C" {
#endif

void pebs_vulcan_init(void);
void pebs_vulcan_update_bw(double dram_bw, double nvm_bw);
void pebs_vulcan_update_accesses(uint64_t va, double new_accesses);
void pebs_vulcan_add_page(uint64_t va);
void pebs_vulcan_remove_page(uint64_t va);
size_t pebs_vulcan_get_page_ranks(struct score_entry *scores_out);
void pebs_vulcan_setup_LLM_heuristic();

#ifdef __cplusplus
}
#endif