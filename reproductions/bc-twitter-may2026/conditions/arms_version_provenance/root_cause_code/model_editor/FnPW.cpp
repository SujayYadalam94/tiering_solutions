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
#define MODEL_NUM_FEATURES 12

static inline double model_discount_scale()
{
#if MODEL_DISCOUNT_PERCENT <= 0
    return 0.0;
#elif MODEL_DISCOUNT_PERCENT >= 100
    return 0.0;
#else
    return 1.0 / (1.0 - (static_cast<double>(MODEL_DISCOUNT_PERCENT) / 100.0));
#endif
}

static inline double round_to_6(double value)
{
    return std::round(value * 1000000.0) / 1000000.0;
}

static inline double build_ratio_feature(double numerator, double denominator)
{
    if (denominator <= 1e-9)
    {
        return 0.0;
    }

    const double ratio = numerator / denominator;
    if (ratio <= 0.0)
    {
        return 0.0;
    }
    if (ratio >= 1.0)
    {
        return 1.0;
    }
    return ratio;
}

/**
 * Extract features from page_info into the feature buffer
 * This is where you define which features from the page are used for prediction
 * Adjust the feature extraction based on your model's training configuration
 */
static inline void extract_features(struct data_row &row, double *features)
{
    // Exact order from models/model_discounted_reward_*_*.txt:
    // ewma_2 ewma_5 ewma_20 ewma_100 global_avg_accesses_model group_neg_mean
    // group_pos_mean group_0_mean age r_ratio_20 r_ratio_100 group_ewma5_var

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
    const double r_ratio_20 = round_to_6(build_ratio_feature(row.ewma_20_r, row.ewma_20));
    const double r_ratio_100 = round_to_6(build_ratio_feature(row.ewma_100_r, row.ewma_100));

    features[i++] = round_to_6(row.ewma_2);
    features[i++] = round_to_6(row.ewma_5);
    features[i++] = round_to_6(row.ewma_20);
    features[i++] = round_to_6(row.ewma_100);
    features[i++] = round_to_6(row.global_avg_accesses_model);
    features[i++] = group_neg_mean;
    features[i++] = group_pos_mean;
    features[i++] = group_0_mean;
    features[i++] = static_cast<double>(row.age > 100 ? 100 : row.age);
    features[i++] = r_ratio_20;
    features[i++] = r_ratio_100;
    features[i++] = round_to_6(row.group_ewma5_var);

    assert(i == MODEL_NUM_FEATURES);
}

static void model_predict_batch_impl(std::vector<struct data_row> &rows,
                                     const std::vector<std::shared_ptr<struct page_info>> &pages, bool update_history)
{
    assert(rows.size() == pages.size());

    std::vector<double> outputs(rows.size(), 0.0);
    const double discount_scale = model_discount_scale();

#if VIRTUAL_FEATURES_ENABLED
    for (size_t i = 0; i < rows.size(); ++i)
    {
        double features[MODEL_NUM_FEATURES] = {0.0};
        extract_features(rows[i], features);
#if USE_MODEL == (true)
        forest_root(features, &outputs[i], 0, 1);
        outputs[i] += discount_scale *
                      static_cast<double>(std::max(rows[i].virtual_missed_ewma_100, rows[i].virtual_missed_accesses));
#else
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
        if (update_history)
        {
            std::lock_guard<std::mutex> guard(pages[i]->page_lock);
            // In model/logging mode, extract_row rewrites row.step to page->virtual_step.
            const uint64_t smoothing_step = static_cast<uint64_t>(rows[i].step);
            if (!VIRTUAL_FEATURES_ENABLED || pages[i]->last_model_score_step != smoothing_step)
            {
                pages[i]->push_model_score(static_cast<float>(outputs[i]));
                pages[i]->last_model_score_step = smoothing_step;
            }
        }
        rows[i].model_score = outputs[i];
    }
}

void model_predict_batch(std::vector<struct data_row> &rows,
                         const std::vector<std::shared_ptr<struct page_info>> &pages)
{
    model_predict_batch_impl(rows, pages, true);
}

void model_predict_batch_observe(std::vector<struct data_row> &rows,
                                 const std::vector<std::shared_ptr<struct page_info>> &pages)
{
    model_predict_batch_impl(rows, pages, false);
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