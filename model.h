#pragma once

#include "groups.h"
#include <stdbool.h>
#include <stdint.h>

extern "C" void forest_root(double *data, double *out, int offset, int n_preds);

// Forward declaration
struct page_info;

double model_predict(struct data_row &row);