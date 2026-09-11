#pragma once

#include "groups.h"
#include <stdbool.h>
#include <stdint.h>
#include <vector>

extern "C" void forest_root(double *data, double *out, int offset, int n_preds);

// Forward declaration
struct page_info;

double model_predict(struct data_row &row, struct page_info &page);
void model_predict_batch(std::vector<struct data_row> &rows,
                         const std::vector<std::shared_ptr<struct page_info>> &pages);
void model_predict_batch_observe(std::vector<struct data_row> &rows,
                                 const std::vector<std::shared_ptr<struct page_info>> &pages);