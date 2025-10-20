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
#define MODEL_NUM_FEATURES 19


/**
 * Extract features from hemem_page into the feature buffer
 * This is where you define which features from the page are used for prediction
 * Adjust the feature extraction based on your model's training configuration
 */
static void extract_features(struct hemem_page *page, struct group_tracker *grp_tracker, size_t count_total, double cpu_usage, double *features)
{
    features[0] = (double)page->w[0];
    features[1] = (double)page->w[1];
    features[2] = (double)page->w[3];
    features[3] = (double)page->w_w[0];
    features[4] = (double)page->w_w[1];
    features[5] = (double)page->w_w[3];
    features[6] = (double)page->global_count_since_top50_percent_ewma5;
    features[7] = (double)count_total;

    struct page_group *pg = try_get_group(grp_tracker, page->va, -1);
    features[8] = pg ? pg->avg : 0.0;

    pg = try_get_group(grp_tracker, page->va, 1);
    features[9] = pg ? pg->avg : 0.0;

    pg = try_get_group(grp_tracker, page->va, -2);
    features[10] = pg ? pg->avg : 0.0;

    pg = try_get_group(grp_tracker, page->va, 2);
    features[11] = pg ? pg->avg : 0.0;

    pg = try_get_group(grp_tracker, page->va, 0);
    features[12] = pg ? pg->avg : 0.0;

    features[13] = (double)page->age;
    features[14] = cpu_usage;
    features[15] = (double)page->malloc_size_ewma[3];
    features[16] = (double)page->malloc_call_ewma[3];
    features[17] = (double)page->min_malloc_bytes;
    features[18] = (double)page->max_malloc_bytes;

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

    return out;
}
