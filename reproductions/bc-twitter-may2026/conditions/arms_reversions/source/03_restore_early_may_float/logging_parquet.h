#pragma once

#include <cstddef>
#include <string>

struct data_row;

// Called after reward finalization; throws on failure. Only defined and linked
// in PRINT_TRAINING_DATA builds. No Arrow types leak into shared headers.
void write_training_parquet(const std::string &path, const data_row *rows, size_t count);
