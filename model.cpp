#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "logging.h"
#include "model.h"
#include "page.h"

// Number of features - this should match your model's training configuration
// Set to a placeholder value; adjust based on your actual model
#define MODEL_NUM_FEATURES 13

/**
 * Extract features from page_info into the feature buffer
 * This is where you define which features from the page are used for prediction
 * Adjust the feature extraction based on your model's training configuration
 */
static inline void extract_features(struct page_info *page, struct group_tracker *grp_tracker, size_t count_total,
                                    double cpu_usage, double *features)
{
    int8_t i = 0;
    features[i++] = (double)page->w_perc[0];
    features[i++] = (double)page->w_perc[1];
    features[i++] = (double)page->w_perc[2];
    features[i++] = (double)page->w_perc[3];
    // features[i++] = (double)page->global_count_since_top50_percent_ewma5;

    struct page_group *pg = try_get_group(grp_tracker, page->va, -1);
    features[i++] = pg ? pg->avg_perc : 0.0;

    pg = try_get_group(grp_tracker, page->va, 1);
    features[i++] = pg ? pg->avg_perc : 0.0;

    pg = try_get_group(grp_tracker, page->va, -2);
    features[i++] = pg ? pg->avg_perc : 0.0;
    pg = try_get_group(grp_tracker, page->va, 2);
    features[i++] = pg ? pg->avg_perc : 0.0;

    pg = try_get_group(grp_tracker, page->va, -3);
    features[i++] = pg ? pg->avg_perc : 0.0;

    pg = try_get_group(grp_tracker, page->va, 3);
    features[i++] = pg ? pg->avg_perc : 0.0;

    pg = try_get_group(grp_tracker, page->va, 0);
    features[i++] = pg ? pg->avg_perc : 0.0;

    features[i++] = (double)page->age_count_total;
    // features[i++] = (double)cpu_usage;
    features[i++] = (double)page->malloc_call_ewma[3]; // Long-term malloc call ewma

    assert(i == MODEL_NUM_FEATURES);
}

double model_predict(struct page_info *page, struct group_tracker *grp_tracker, size_t count_total, double cpu_usage)
{
#if USE_MODEL == (true)
    if (page == NULL)
    {
        std::cout << "Null page pointer in model_predict" << std::endl;
        return 0.0;
    }

    double feature_buffer[MODEL_NUM_FEATURES];
    double out;

    // Extract features from the page
    extract_features(page, grp_tracker, count_total, cpu_usage, feature_buffer);

    // Perform prediction using lleaves
    // lleaves provides fast inference optimized for LightGBM models

    forest_root(feature_buffer, &out, 0, 1);
    if (out < 0.0)
        out = 0.0;

    return out;
#endif
    return 0.0; // If model usage is disabled, return 0.0
}