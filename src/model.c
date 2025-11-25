#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>

#include "model.h"
#include "hemem_page.h"
#include "logging.h"

// Number of features - this should match your model's training configuration
// Set to a placeholder value; adjust based on your actual model
#define MODEL_NUM_FEATURES 12


/**
 * Extract features from hemem_page into the feature buffer
 * This is where you define which features from the page are used for prediction
 * Adjust the feature extraction based on your model's training configuration
 */
static void extract_features(struct hemem_page *page, struct group_tracker *grp_tracker, size_t count_total, double cpu_usage, double *features)
{
    features[0] = (double)page->w_perc[0];
    features[1] = (double)page->w_perc[1];
    features[2] = (double)page->w_perc[2];
    features[3] = (double)page->w_perc[3];
    //features[i++] = (double)page->global_count_since_top50_percent_ewma5;

    struct page_group *pg = try_get_group(grp_tracker, page->va, -1);
    features[4] = pg ? pg->avg_perc : 0.0;

    pg = try_get_group(grp_tracker, page->va, 1);
    features[5] = pg ? pg->avg_perc : 0.0;

    pg = try_get_group(grp_tracker, page->va, -2);
    features[6] = pg ? pg->avg_perc : 0.0;

    pg = try_get_group(grp_tracker, page->va, 2);
    features[7] = pg ? pg->avg_perc : 0.0;

    pg = try_get_group(grp_tracker, page->va, -3);
    features[8] = pg ? pg->avg_perc : 0.0;

    pg = try_get_group(grp_tracker, page->va, 3);
    features[9] = pg ? pg->avg_perc : 0.0;

    pg = try_get_group(grp_tracker, page->va, 0);
    features[10] = pg ? pg->avg_perc : 0.0;

    features[11] = (double)page->age_count_total;
    //features[i++] = (double)cpu_usage;
    //features[12] = (double)page->malloc_call_ewma[3]; // Long-term malloc call ewma
}

double model_predict(struct hemem_page *page, struct group_tracker *grp_tracker, size_t count_total, double cpu_usage)
{
    if (page == NULL) {
        LOG_ERROR("Null page pointer in model_predict\n");
        return 0.0;
    }

    double feature_buffer[MODEL_NUM_FEATURES];
    double out;

    // Extract features from the page
    extract_features(page, grp_tracker, count_total, cpu_usage, feature_buffer);

    // Perform prediction using lleaves
    // lleaves provides fast inference optimized for LightGBM models

    forest_root(feature_buffer, &out, 0, 1);
    if (out < 0.0) out = 0.0;

    return out;
}
