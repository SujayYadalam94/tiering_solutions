#pragma once

#include "groups.h"
#include <stdbool.h>
#include <stdint.h>

constexpr size_t MODEL_NUM_FEATURES = 16;

extern "C" void forest_root(double *data, double *out, int offset, int n_preds);

// Forward declaration
struct model_features;
struct page_info;

double model_predict(struct data_row &row, struct page_info &page);
double model_predict(const struct model_features &features, struct page_info &page);
double model_predict_from_buffer(double *feature_buffer, struct page_info &page);