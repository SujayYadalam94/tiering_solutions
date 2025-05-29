#pragma once

#include "defs.h"
#include "groups.h"
#include <cmath>
#include <pthread.h>

enum pagetypes { HUGEP = 0, BASEP = 1, NPAGETYPES };
enum pbuftype { DRAMREAD = 0, NVMREAD = 1, WRITE = 2, NPBUFTYPES };
// defaults to Model since this would be all 0
enum prediction_type { MODEL = 0, ARMS = 1, NPREDICTIONTYPES };

struct hemem_page {
    uint64_t va;
    uint64_t devdax_offset;
    bool in_dram;
    enum pagetypes pt;
    volatile bool migrating;
    bool present;
    uint16_t accesses[NPBUFTYPES][2];
    pthread_mutex_t page_lock;

    float w[WINDOW_SIZE];

    float score;
    float prev_score;
    uint16_t hot_age;
    bool can_promote;

    struct hemem_page *next, *prev;
    struct fifo_list *list;

    float accuracy;
    float model_score;
    float arms_score;
    prediction_type model_selection;
    uint64_t count_above_mean;
    uint64_t count_below_mean;
    uint64_t _padding[3];
};
static_assert(sizeof(struct hemem_page) == 192);

static void reset_page_access_fields(struct hemem_page *page) {
    for (int i = 0; i < NPBUFTYPES; i++) {
        page->accesses[i][0] = 0;
        page->accesses[i][1] = 0;
    }
    for (int i = 0; i < WINDOW_SIZE; i++) {
        page->w[i] = 0;
    }
    page->hot_age = 0;
    page->prev_score = 0;
    page->accuracy = 0;
}

static inline float ewma(const float &old, const float &new_val,
                         const float &alpha, const bool &force = false) {
    // If unset, return the new value
    if (force) {
        return new_val;
    }
    return (1. - alpha) * old + (alpha * new_val);
}

static inline float calculate_accesses(struct hemem_page *&page,
                                       volatile uint8_t &prev_access_version) {
    return HF_SAMPLE_PERIOD *
           (page->accesses[DRAMREAD][prev_access_version] +
            page->accesses[NVMREAD][prev_access_version] +
            (WRITES_WEIGHT * page->accesses[WRITE][prev_access_version]));
}

static inline void update_window(struct hemem_page *page,
                                 volatile uint8_t &prev_access_version,
                                 const enum sampling_modes &sampling_mode) {
    uint32_t accesses = calculate_accesses(page, prev_access_version);

    if (sampling_mode == DEFAULT_SAMPLING) {
        constexpr float scaler = DEFAULT_SAMPLE_PERIOD / HF_SAMPLE_PERIOD;
        for (uint8_t i = 0; i < WINDOW_SIZE; i++) {
            page->w[i] = ewma(page->w[i], accesses * scaler, w_ewma_alpha[i]);
        }
    } else if (sampling_mode == HIGH_FIDELITY) {
        for (uint8_t i = 0; i < WINDOW_SIZE; i++) {
            page->w[i] = ewma(page->w[i], accesses, w_ewma_alpha[i]);
        }
    }

    // group["EWMA_2"] > group["EWMA_100"]
    if (page->w[0] > page->w[2]) {
        page->count_above_mean += accesses;
        page->count_below_mean = 0;
    } else {
        page->count_above_mean = 0;
        page->count_below_mean += accesses;
    }
}

static inline float compute_score(const struct hemem_page *page,
                                  const float *bias) {
    // Update the score (average of the window)
    // TODO: The model inference goes here
    // Leave this code here. It will be useful for guardrails
    float score = 0;
    for (int i = 0; i < WINDOW_SIZE; i++) {
        score += page->w[i] * bias[i];
    }
    return score;
}