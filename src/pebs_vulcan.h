#pragma once

#include <stdint.h>
#include "hemem.h"
#include "pebs.h"

#ifdef __cplusplus
extern "C" {
#endif

void pebs_vulcan_init(void);
void pebs_vulcan_update_bw(double dram_bw, double nvm_bw);
void pebs_vulcan_update_accesses(uint64_t va, double new_accesses);
void pebs_vulcan_add_page(uint64_t va);
void pebs_vulcan_remove_page(uint64_t va);
void pebs_vulcan_get_all_ranks(void **all_pages, uint64_t *all_vas, int pages_cnt, struct score_entry *scores_out, int *num_results);
void pebs_vulcan_setup_LLM_heuristic();

#ifdef __cplusplus
}
#endif