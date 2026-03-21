#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "logging.h"
#include "model.h"
#include "page.h"

/**
 * Extract features from page_info into the feature buffer
 * This is where you define which features from the page are used for prediction
 * Adjust the feature extraction based on your model's training configuration
 */
static inline void extract_features(struct data_row &row, double *features)
{

    // ewma_5_perc ewma_20_perc ewma_100_perc global_avg_accesses_perc group_neg_mean_ewma5_perc
    // group_pos_mean_ewma5_perc group_0_mean_ewma5_perc

    int8_t i = 0;
    /*features[i++] = row.ewma_2_perc;
    features[i++] = row.ewma_5_perc;
    features[i++] = row.ewma_20_perc;
    features[i++] = row.ewma_100_perc;
    // features[i++] = (double)page->global_count_since_top50_percent_ewma5;

    features[i++] = row.groups_perc[-1 + 7]; // -1 offset
    features[i++] = row.groups_perc[1 + 7];  // 1 offset
    features[i++] = row.groups_perc[-2 + 7]; // -2 offset
    features[i++] = row.groups_perc[2 + 7];  // 2 offset
    features[i++] = row.groups_perc[-3 + 7]; // -3 offset
    features[i++] = row.groups_perc[3 + 7];  // 3 offset
    features[i++] = row.groups_perc[0 + 7];  // 0 offset
    features[i++] = row.age_count_total;*/

    // ewma_2_perc ewma_5_perc ewma_20_perc ewma_100_perc
    // ewma_2_w_perc ewma_5_w_perc ewma_20_w_perc ewma_100_w_perc
    // global_avg_accesses_perc group_neg_mean_perc group_pos_mean_perc group_0_mean_perc
    // gap4 read_write_gap3 ewma_var_100 group_ewma5_var
    // gap4 = ewma_5_perc - global_avg_accesses_perc
    // read_write_gap3 = ewma_100_r_perc - ewma_100_w_perc

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

    assert(i == MODEL_NUM_FEATURES);
}

static inline void extract_features_from_model_features(const struct model_features &row, double *features)
{
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
    features[i++] = row.groups_perc_neg_sum;
    features[i++] = row.groups_perc_pos_sum;
    features[i++] = row.groups_perc_center;
    features[i++] = row.gap4;
    features[i++] = row.read_write_gap3;
    features[i++] = row.ewma_var_100;
    features[i++] = row.group_ewma5_var;

    assert(i == MODEL_NUM_FEATURES);
}

static inline double infer_forest(double *feature_buffer)
{
    double out = 0.0;
    forest_root(feature_buffer, &out, 0, 1);
    if (out < 0.0)
    {
        out = 0.0;
    }
    return out;
}

double model_predict_from_buffer(double *feature_buffer, struct page_info &page)
{
    double out = 0.0;

#if USE_MODEL == (true)
    out = infer_forest(feature_buffer);
#else
    (void)feature_buffer;
#endif

    page.push_model_score(static_cast<float>(out));
    return out;
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
    out = model_predict_from_buffer(feature_buffer, page);
#else
    (void)page;
#endif

    row.model_score = out;

    return out; // If model usage is disabled, out stays at 0.0
}

double model_predict(const struct model_features &features, struct page_info &page)
{
    double out = 0.0;

#if USE_MODEL == (true)
    double feature_buffer[MODEL_NUM_FEATURES];
    extract_features_from_model_features(features, feature_buffer);
    out = model_predict_from_buffer(feature_buffer, page);
#else
    (void)features;
    (void)page;
#endif

    return out;
}