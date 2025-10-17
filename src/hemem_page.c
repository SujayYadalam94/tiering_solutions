#include "hemem_page.h"

#include <stdlib.h>

void reset_page_access_fields(struct hemem_page *page) {
    for (int i = 0; i < NPBUFTYPES; i++) {
        page->accesses[i][0] = 0;
        page->accesses[i][1] = 0;
    }
    for (int i = 0; i < WINDOW_SIZE; i++) {
        page->w[i] = 0;
        page->w_r[i] = 0;
        page->w_w[i] = 0;
        page->malloc_call_ewma[i] = 0;
        page->malloc_size_ewma[i] = 0;
    }

    page->cumsum_reads = 0;
    page->cumsum_writes = 0;

    page->age = 0;
    page->hot_age = 0;
    page->prev_score = 0;
    page->accuracy = 0;

    page->prev_count = 0;

    page->read_bytes = 0;
    page->write_bytes = 0;
    page->read_syscalls = 0;
    page->write_syscalls = 0;
    page->sum_malloc_bytes = 0;
    page->min_malloc_bytes = -1;
    page->max_malloc_bytes = -1;
    page->malloc_call = 0;
}

float ewma(const float yp, const float x, const float alpha) {
    return (1. - alpha) * yp + (alpha * x);
}

float adjusted_ewma(const float yp, const float x,
                                  const float denom) {
    return yp * (1 - (1 / denom)) + (x / denom);
}

float calculate_reads(const struct hemem_page *page,
                                    volatile uint8_t prev_access_version) {
    return (page->accesses[DRAMREAD][prev_access_version] +
            page->accesses[NVMREAD][prev_access_version]);
}

float calculate_writes(const struct hemem_page *page,
                                     volatile uint8_t prev_access_version) {
    return page->accesses[WRITE][prev_access_version];
}

float calculate_accesses(const struct hemem_page *page,
                                       volatile uint8_t prev_access_version) {
    return calculate_reads(page, prev_access_version) +
           NVM_WRITES_WEIGHT * calculate_writes(page, prev_access_version);
}

void update_window(struct hemem_page *page,
                                 volatile uint8_t prev_access_version,
                                 const enum sampling_modes sampling_mode) {
    page->read_bytes = 0;
    page->write_bytes = 0;
    page->read_syscalls = 0;
    page->write_syscalls = 0;
    page->sum_malloc_bytes = 0;
    page->malloc_call = 0;

    page->age++;
    
    page->count = calculate_accesses(page, prev_access_version);
    page->reads = calculate_reads(page, prev_access_version);
    page->writes = calculate_writes(page, prev_access_version);

    page->accesses[DRAMREAD][prev_access_version] = 0;
    page->accesses[NVMREAD][prev_access_version] = 0;
    page->accesses[WRITE][prev_access_version] = 0;

    page->cumsum_reads += page->reads;
    page->cumsum_writes += page->writes;

    page->diff = ((int32_t)page->count) - ((int32_t)page->prev_count);
    page->prev_count = page->count;

    float scaler = sampling_mode == DEFAULT_SAMPLING
                       ? DEFAULT_SAMPLE_PERIOD / HF_SAMPLE_PERIOD
                       : 1.0;
    for (uint8_t i = 0; i < WINDOW_SIZE; i++) {
        page->w[i] = adjusted_ewma(page->w[i], page->count * scaler,
                                   get_adjusted_ewma_denom(i, page->age));
        page->w_r[i] = adjusted_ewma(page->w_r[i], page->reads,
                                     get_adjusted_ewma_denom(i, page->age));
        page->w_w[i] = adjusted_ewma(page->w_w[i], page->writes,
                                     get_adjusted_ewma_denom(i, page->age));
    }
}

void
update_derivative_features(struct hemem_page *page,
                           size_t rank, size_t num_sorted_pages,
                           size_t count_total) {
    if ((page->count > 0.8f * page->prev_count &&
         page->count < 1.2f * page->prev_count) ||
        abs((int32_t)page->count - (int32_t)page->prev_count) < 2) {
        page->global_count_similar = page->global_count_similar + count_total;
    } else {
        page->global_count_similar = 0;
    }

    // time since top 1% and 50% in ewma5
    page->rank = rank;

    size_t top1_percent_index = num_sorted_pages / 100;
    size_t top50_percent_index = num_sorted_pages / 2;

    for (uint8_t i = 0; i < WINDOW_SIZE; i++) {
        page->malloc_call_ewma[i] = adjusted_ewma(page->malloc_call_ewma[i], page->malloc_call,
                                     get_adjusted_ewma_denom(i, page->age));
        page->malloc_size_ewma[i] = adjusted_ewma(page->malloc_size_ewma[i], page->sum_malloc_bytes,
                                     get_adjusted_ewma_denom(i, page->age));
    }

    if (page->age == 0 && page->count == 0) {
        page->global_count_since_top1_percent_ewma5 = 0;
        page->global_count_since_top50_percent_ewma5 = 0;
        return;
    }

    if (rank <= top1_percent_index) {
        page->global_count_since_top1_percent_ewma5 =
            fmin(page->global_count_since_top1_percent_ewma5,
                     0) -
            count_total;
    } else {
        page->global_count_since_top1_percent_ewma5 =
            fmax(page->global_count_since_top1_percent_ewma5,
                     0) +
            count_total;
    }

    if (rank <= top50_percent_index) {
        page->global_count_since_top50_percent_ewma5 =
            fmin(page->global_count_since_top50_percent_ewma5,
                     0) -
            count_total;
    } else {
        page->global_count_since_top50_percent_ewma5 =
            fmax(page->global_count_since_top50_percent_ewma5,
                     0) +
            count_total;
    }
}

float compute_score(const struct hemem_page *page,
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