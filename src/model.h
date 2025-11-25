#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "groups.h"

extern void forest_root(double *data, double *out, int offset, int n_preds);

// Forward declaration
struct hemem_page;

/**
 * Initialize the LightGBM model from a file
 * @param model_path Path to the LightGBM model file (text format)
 * @return 0 on success, -1 on failure
 */
int model_init(const char *model_path);


double model_predict(struct hemem_page *page, struct group_tracker *grp_tracker, size_t count_total, double cpu_usage);

/**
 * Clean up and free model resources
 */
void model_cleanup(void);

/**
 * Check if model is initialized and ready for predictions
 * @return true if model is loaded and ready, false otherwise
 */
bool model_is_ready(void);
