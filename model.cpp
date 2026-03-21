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
#define MODEL_NUM_FEATURES 17

/**
 * Extract features from page_info into the feature buffer
 * This is where you define which features from the page are used for prediction
 * Adjust the feature extraction based on your model's training configuration
 */
static inline void extract_features(struct data_row &row, double *features)
{

    // ewma_2_perc ewma_5_perc ewma_20_perc ewma_100_perc ewma_2_w_perc ewma_5_w_perc ewma_20_w_perc ewma_100_w_perc
    // global_avg_accesses_perc group_neg_mean_perc group_pos_mean_perc group_0_mean_perc gap4 read_write_gap3
    // ewma_var_100 group_ewma5_var

    int8_t i = 0;

    features[i++] = row.ewma_2_perc;
    features[i++] = row.ewma_5_perc;
    features[i++] = row.ewma_20_perc;
    features[i++] = row.ewma_100_perc;
    features[i++] = row.ewma_2_w_perc;
    features[i++] = row.ewma_5_w_perc;
    features[i++] = row.ewma_20_w_perc;
    features[i++] = row.ewma_100_w_perc;
    features[i++] = row.global_avg_accesses_perc;
    features[i++] = row.groups_perc[-1 + 7] + row.groups_perc[-2 + 7] + row.groups_perc[-3 + 7];
    features[i++] = row.groups_perc[1 + 7] + row.groups_perc[2 + 7] + row.groups_perc[3 + 7];
    features[i++] = row.groups_perc[0 + 7];

    features[i++] = row.gap4;
    features[i++] = row.read_write_gap3;

    features[i++] = row.ewma_var_100;
    features[i++] = row.group_ewma5_var;
    features[i++] = row.age_count_total;

    assert(i == MODEL_NUM_FEATURES);
}

double model_predict(struct data_row &row, struct page_info &page)
{
    double out = 0.0;

#if USE_MODEL == (true)
    double feature_buffer[MODEL_NUM_FEATURES];

    // Extract features from the page
    extract_features(row, feature_buffer);

    // Perform prediction using lleaves
    // lleaves provides fast inference optimized for LightGBM models

    forest_root(feature_buffer, &out, 0, 1);
    if (out < 0.0)
        out = 0.0;
#endif

    page.push_model_score(static_cast<float>(out));
    row.model_score = out;

    return out; // If model usage is disabled, out stays at 0.0
}