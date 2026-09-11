#pragma once

#include "groups.h"
#include <chrono>
#include <stdbool.h>
#include <stdint.h>
#include <vector>

extern "C" void forest_root(double *data, double *out, int offset, int n_preds);

// Forward declaration
struct page_info;

struct model_score_timing
{
#if MODEL_TIMING_TELEMETRY_ENABLED
    using clock = std::chrono::steady_clock;
    clock::time_point start = clock::now();
    clock::time_point features_end;
    clock::time_point inference_end;
#endif

    void finish_features();
    void finish_inference();
    void finish(std::vector<struct data_row> &rows);
};

double model_predict(struct data_row &row, struct page_info &page);
void model_predict_batch(std::vector<struct data_row> &rows,
                         const std::vector<std::shared_ptr<struct page_info>> &pages);
void model_predict_batch_observe(std::vector<struct data_row> &rows,
                                 const std::vector<std::shared_ptr<struct page_info>> &pages,
                                 model_score_timing *timing = nullptr);
