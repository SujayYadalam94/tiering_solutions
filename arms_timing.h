#pragma once

#include "logging.h"
#include <chrono>

// Measure the actual ARMS window/score pass, excluding training-only feature
// extraction, row logging, ranking, and migration. Disabled builds read no clock.
struct arms_score_timing
{
#if ARMS_TIMING_TELEMETRY_ENABLED
    using clock = std::chrono::steady_clock;
    clock::time_point start{};
    clock::time_point features_end{};
    clock::time_point scores_end{};
#endif

    void begin()
    {
#if ARMS_TIMING_TELEMETRY_ENABLED
        start = clock::now();
#endif
    }

    void finish_features()
    {
#if ARMS_TIMING_TELEMETRY_ENABLED
        features_end = clock::now();
#endif
    }

    void finish_scores()
    {
#if ARMS_TIMING_TELEMETRY_ENABLED
        scores_end = clock::now();
#endif
    }

    void apply(data_row &row) const
    {
#if ARMS_TIMING_TELEMETRY_ENABLED
        row.arms_feature_aggregation_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(features_end - start).count();
        row.arms_scoring_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(scores_end - features_end).count();
        row.arms_score_total_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(scores_end - start).count();
#else
        (void)row;
#endif
    }
};
