#include <assert.h>
#include <cmath>
#include <mutex>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "logging.h"
#include "model.h"
#include "page.h"
#include <vector>

// Number of features - must match model file feature_names count.
#define MODEL_NUM_FEATURES 14

static inline double round_to_6(double value)
{
    return std::round(value * 1000000.0) / 1000000.0;
}

// Keep inference work observable in non-model virtual-feature builds.
static volatile double inference_work_sink = 0.0;

/**
 * Extract features from page_info into the feature buffer
 * This is where you define which features from the page are used for prediction
 * Adjust the feature extraction based on your model's training configuration
 */
static inline void extract_features(struct data_row &row, double *features)
{
    // Exact order from models/model_discounted_reward_90_XSBench_l2.txt:
    // ewma_2 ewma_5 ewma_20 ewma_100 ewma_2_w ewma_5_w ewma_20_w ewma_100_w
    // global_avg_accesses_model group_neg_mean group_pos_mean group_0_mean gap4 read_write_gap3

    int8_t i = 0;

    const double group_m1 = round_to_6(row.groups[-1 + 7]);
    const double group_m2 = round_to_6(row.groups[-2 + 7]);
    const double group_m3 = round_to_6(row.groups[-3 + 7]);
    const double group_p1 = round_to_6(row.groups[1 + 7]);
    const double group_p2 = round_to_6(row.groups[2 + 7]);
    const double group_p3 = round_to_6(row.groups[3 + 7]);

    const double group_neg_mean = group_m1 + group_m2 + group_m3;
    const double group_pos_mean = group_p1 + group_p2 + group_p3;
    const double group_0_mean = round_to_6(row.groups[0 + 7]);

    features[i++] = round_to_6(row.ewma_2);
    features[i++] = round_to_6(row.ewma_5);
    features[i++] = round_to_6(row.ewma_20);
    features[i++] = round_to_6(row.ewma_100);
    features[i++] = round_to_6(row.ewma_2_w);
    features[i++] = round_to_6(row.ewma_5_w);
    features[i++] = round_to_6(row.ewma_20_w);
    features[i++] = round_to_6(row.ewma_100_w);
    features[i++] = round_to_6(row.global_avg_accesses_model);
    features[i++] = group_neg_mean;
    features[i++] = group_pos_mean;
    features[i++] = group_0_mean;

    features[i++] = round_to_6(row.gap4);
    features[i++] = round_to_6(row.read_write_gap3);

    assert(i == MODEL_NUM_FEATURES);
}

void model_predict_batch(std::vector<struct data_row> &rows,
                         const std::vector<std::shared_ptr<struct page_info>> &pages)
{
    assert(rows.size() == pages.size());

    std::vector<double> outputs(rows.size(), 0.0);

#if VIRTUAL_FEATURES_ENABLED
    for (size_t i = 0; i < rows.size(); ++i)
    {
        double features[MODEL_NUM_FEATURES] = {0.0};
        extract_features(rows[i], features);
#if USE_MODEL == (true)
        forest_root(features, &outputs[i], 0, 1);
#else
        // Keep non-model builds doing feature work without linking any model object.
        double feature_work = 0.0;
        for (int j = 0; j < MODEL_NUM_FEATURES; ++j)
        {
            feature_work += features[j];
        }
        inference_work_sink += feature_work;
        outputs[i] = 0.0;
#endif
    }
#endif

    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (outputs[i] < 0.0)
        {
            outputs[i] = 0.0;
        }
        {
            std::lock_guard<std::mutex> guard(pages[i]->page_lock);
            if (!VIRTUAL_FEATURES_ENABLED || pages[i]->last_model_score_step != rows[i].step)
            {
                pages[i]->push_model_score(static_cast<float>(outputs[i]));
                pages[i]->last_model_score_step = rows[i].step;
            }
        }
        rows[i].model_score = outputs[i];
    }
}

double model_predict(struct data_row &row, struct page_info &page)
{
    std::vector<struct data_row> rows = {row};
    std::shared_ptr<struct page_info> page_ref(&page, [](struct page_info *) {});
    std::vector<std::shared_ptr<struct page_info>> pages = {page_ref};
    model_predict_batch(rows, pages);
    row.model_score = rows[0].model_score;
    return row.model_score;
}