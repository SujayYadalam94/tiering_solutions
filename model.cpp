#include <assert.h>
#include <cmath>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "logging.h"
#include "model.h"
#include "page.h"
#include <vector>

// Number of features - this should match your model's training configuration
#define MODEL_NUM_FEATURES 16

static inline double round_to_6(double value)
{
    return std::round(value * 1000000.0) / 1000000.0;
}

/**
 * Extract features from page_info into the feature buffer
 * This is where you define which features from the page are used for prediction
 * Adjust the feature extraction based on your model's training configuration
 */
static inline void extract_features(struct data_row &row, double *features)
{

    // ewma_2_perc ewma_5_perc ewma_20_perc ewma_100_perc
    // ewma_2_w_perc ewma_5_w_perc ewma_20_w_perc ewma_100_w_perc
    // global_avg_accesses_perc group_neg_mean_perc group_pos_mean_perc group_0_mean_perc
    // gap4 read_write_gap3 ewma_var_100 group_ewma5_var

    int8_t i = 0;

    features[i++] = round_to_6(row.ewma_2_perc);
    features[i++] = round_to_6(row.ewma_5_perc);
    features[i++] = round_to_6(row.ewma_20_perc);
    features[i++] = round_to_6(row.ewma_100_perc);
    features[i++] = round_to_6(row.ewma_2_w_perc);
    features[i++] = round_to_6(row.ewma_5_w_perc);
    features[i++] = round_to_6(row.ewma_20_w_perc);
    features[i++] = round_to_6(row.ewma_100_w_perc);
    features[i++] = round_to_6(row.global_avg_accesses_perc);
    features[i++] = round_to_6(round_to_6(row.groups_perc[-1 + 7]) + round_to_6(row.groups_perc[-2 + 7]) +
                               round_to_6(row.groups_perc[-3 + 7]));
    features[i++] = round_to_6(round_to_6(row.groups_perc[1 + 7]) + round_to_6(row.groups_perc[2 + 7]) +
                               round_to_6(row.groups_perc[3 + 7]));
    features[i++] = round_to_6(row.groups_perc[0 + 7]);

    features[i++] = round_to_6(row.gap4);
    features[i++] = round_to_6(row.read_write_gap3);

    features[i++] = round_to_6(row.ewma_var_100);
    features[i++] = round_to_6(row.group_ewma5_var);

    assert(i == MODEL_NUM_FEATURES);
}

void model_predict_batch(std::vector<struct data_row> &rows, const std::vector<struct page_info *> &pages)
{
    assert(rows.size() == pages.size());

    std::vector<double> outputs(rows.size(), 0.0);

#if USE_MODEL == (true)
    for (size_t i = 0; i < rows.size(); ++i)
    {
        double features[MODEL_NUM_FEATURES] = {0.0};
        extract_features(rows[i], features);
        forest_root(features, &outputs[i], 0, 1);
    }
#endif

    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (outputs[i] < 0.0)
        {
            outputs[i] = 0.0;
        }
        pages[i]->push_model_score(static_cast<float>(outputs[i]));
        rows[i].model_score = outputs[i];
    }
}

double model_predict(struct data_row &row, struct page_info &page)
{
    std::vector<struct data_row> rows = {row};
    std::vector<struct page_info *> pages = {&page};
    model_predict_batch(rows, pages);
    row.model_score = rows[0].model_score;
    return row.model_score;
}