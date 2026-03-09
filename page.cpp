#include "page.h"

#include <math.h>
#include <stdlib.h>

void page_info::reset_page_access_fields()
{
    for (int i = 0; i < NPBUFTYPES; i++)
    {
        this->accesses[i][0] = 0;
        this->accesses[i][1] = 0;
    }
    for (int i = 0; i < WINDOW_SIZE; i++)
    {
        this->w[i] = 0;
        this->w_perc_second_moment[i] = 0;
        this->w_perc_var[i] = 0;
        this->w_r[i] = 0;
        this->w_w[i] = 0;
        this->w_perc[i] = 0;
        this->w_r_perc[i] = 0;
        this->w_w_perc[i] = 0;
        this->rank_perc_ewma[i] = 0;
    }

    this->prot = 0;
    this->flags = 0;
    this->count = 0;
    this->reads = 0;
    this->writes = 0;
    this->read_bytes = 0;
    this->write_bytes = 0;
    this->cumsum_reads = 0;
    this->cumsum_writes = 0;
    this->global_avg_accesses = 0;
    this->global_avg_accesses_perc = 0;
    this->gap4 = 0;
    this->read_write_gap3 = 0;

    this->age = 0;
    this->age_count_total = 0;
    this->non_resetting_ewma100 = 0;
    this->non_resetting_age = 0;
    this->accuracy = 0;
    this->reset_model_score_history();
    this->arms_score = 0;

    this->rank = 0;
    this->rank_perc = 0;
    this->hot_age = 0;
    this->prev_score = 0;

    this->last_seen_scan = 0;
    this->last_access_generation = 0;

    this->prev_count = 0;
    this->global_count_similar = 0;
    this->diff = 0;
    this->seen_pages = 0;
    this->pages_in_dram = 0;

    this->promote_backoff = 0;

    this->num_demotions = 0;
    this->num_promotions = 0;

    this->can_promote = true;

    this->last_logged_row = NULL;
}

void page_info::reset_model_score_history()
{
    for (int i = 0; i < HISTORY_LENGTH; i++)
    {
        this->model_score_history[i] = 0.0f;
    }
    this->model_score_history_count = 0;
    this->model_score_history_index = 0;
}

void page_info::push_model_score(float score)
{
    this->model_score_history[this->model_score_history_index] = score;
    this->model_score_history_index = (this->model_score_history_index + 1) % HISTORY_LENGTH;
    if (this->model_score_history_count < HISTORY_LENGTH)
    {
        this->model_score_history_count++;
    }
}

float page_info::max_model_score_history() const
{
    if (this->model_score_history_count == 0)
    {
        return 0.0f;
    }

    float max_val = this->model_score_history[0];
    for (uint8_t i = 1; i < this->model_score_history_count; i++)
    {
        if (this->model_score_history[i] > max_val)
        {
            max_val = this->model_score_history[i];
        }
    }

    return max_val;
}

float page_info::min_model_score_history() const
{
    if (this->model_score_history_count == 0)
    {
        return 0.0f;
    }

    float min_val = this->model_score_history[0];
    for (uint8_t i = 1; i < this->model_score_history_count; i++)
    {
        if (this->model_score_history[i] < min_val)
        {
            min_val = this->model_score_history[i];
        }
    }

    return min_val;
}

float page_info::average_model_score_history() const
{
    if (this->model_score_history_count == 0)
    {
        return 0.0f;
    }

    float sum = 0.0f;
    for (uint8_t i = 0; i < this->model_score_history_count; i++)
    {
        sum += this->model_score_history[i];
    }

    return sum / static_cast<float>(this->model_score_history_count);
}

float ewma(const float yp, const float x, const float alpha)
{
    return (1. - alpha) * yp + (alpha * x);
}

float adjusted_ewma(const float yp, const float x, const float denom)
{
    return yp * (1 - (1 / denom)) + (x / denom);
}

float page_info::calculate_reads(volatile uint8_t prev_access_version)
{
    return (this->accesses[DRAMREAD][prev_access_version] + this->accesses[NVMREAD][prev_access_version]);
}

float page_info::calculate_writes(volatile uint8_t prev_access_version)
{
    return this->accesses[WRITE][prev_access_version];
}

float page_info::calculate_accesses(volatile uint8_t prev_access_version)
{
    return this->calculate_reads(prev_access_version) + NVM_WRITES_WEIGHT * this->calculate_writes(prev_access_version);
}

void page_info::update_window(volatile uint8_t prev_access_version, const enum sampling_modes sampling_mode)
{
    this->read_bytes = 0;
    this->write_bytes = 0;

    this->age++;
    this->count = this->calculate_accesses(prev_access_version);
    this->reads = this->calculate_reads(prev_access_version);
    this->writes = this->calculate_writes(prev_access_version);

    this->accesses[DRAMREAD][prev_access_version] = 0;
    this->accesses[NVMREAD][prev_access_version] = 0;
    this->accesses[WRITE][prev_access_version] = 0;

    this->cumsum_reads += this->reads;
    this->cumsum_writes += this->writes;

    this->diff = ((int32_t)this->count) - ((int32_t)this->prev_count);
    this->prev_count = this->count;

    float scaler = sampling_mode == DEFAULT_SAMPLING ? DEFAULT_SAMPLE_PERIOD / HF_SAMPLE_PERIOD : 1.0;
    for (uint8_t i = 0; i < WINDOW_SIZE; i++)
    {
        const float denom = get_adjusted_ewma_denom(i, this->age);
        const float scaled_count = this->count * scaler;
        this->w[i] = adjusted_ewma(this->w[i], scaled_count, denom);
        this->w_r[i] = adjusted_ewma(this->w_r[i], this->reads, denom);
        this->w_w[i] = adjusted_ewma(this->w_w[i], this->writes, denom);
    }

    if (this->promote_backoff > 0)
    {
        this->promote_backoff--;
    }
}

void page_info::update_derivative_features(size_t rank, size_t num_sorted_pages, size_t count_total)
{
    (void)rank;
    (void)num_sorted_pages;

    this->age_count_total += count_total;

    const double total_accesses = this->cumsum_reads + (NVM_WRITES_WEIGHT * this->cumsum_writes);
    if (this->age > 0)
    {
        this->global_avg_accesses = total_accesses / static_cast<double>(this->age);
    }
    else
    {
        this->global_avg_accesses = 0.0;
    }
    if (this->age_count_total > 0)
    {
        this->global_avg_accesses_perc = total_accesses / static_cast<double>(this->age_count_total);
    }
    else
    {
        this->global_avg_accesses_perc = 0.0;
    }

    const float count_perc = count_total ? (float)(this->count) / (float)(count_total) : 0.0f;

    for (uint8_t i = 0; i < WINDOW_SIZE; i++)
    {
        const float denom = get_adjusted_ewma_denom(i, this->age);

        this->rank_perc_ewma[i] = adjusted_ewma(this->rank_perc_ewma[i], this->rank_perc, denom);

        this->w_perc[i] = adjusted_ewma(this->w_perc[i], count_perc, denom);
        this->w_r_perc[i] =
            adjusted_ewma(this->w_r_perc[i], count_total ? (double)(this->reads) / (double)(count_total) : 0.0, denom);
        this->w_w_perc[i] =
            adjusted_ewma(this->w_w_perc[i], count_total ? (double)(this->writes) / (double)(count_total) : 0.0, denom);

        // Percentile-based variance (EWMA) using second moment of w_perc
        this->w_perc_second_moment[i] = adjusted_ewma(this->w_perc_second_moment[i], count_perc * count_perc, denom);
        const float var = this->w_perc_second_moment[i] - (this->w_perc[i] * this->w_perc[i]);
        this->w_perc_var[i] = var > 0.0f ? var : 0.0f;
    }

    this->gap4 = this->w_perc[1] - static_cast<float>(this->global_avg_accesses_perc);
    this->read_write_gap3 = this->w_r_perc[3] - this->w_w_perc[3];

#if FULL_LOGS == (true)
    if ((this->count > 0.8f * this->prev_count && this->count < 1.2f * this->prev_count) ||
        abs((int32_t)this->count - (int32_t)this->prev_count) < 2)
    {
        this->global_count_similar = this->global_count_similar + count_total;
    }
    else
    {
        this->global_count_similar = 0;
    }
    // time since top 1% and 50% in ewma5
    this->rank = rank;
    this->rank_perc = (float)rank / (float)num_sorted_pages;
    // size_t top1_percent_index = num_sorted_pages / 100;
    // size_t top50_percent_index = num_sorted_pages / 2;

    if (this->age == 0 && this->count == 0)
    {
        this->global_count_since_top1_percent_ewma5 = 0;
        this->global_count_since_top50_percent_ewma5 = 0;
        return;
    }

    if (this->rank_perc <= 0.01f)
    {
        this->global_count_since_top1_percent_ewma5 =
            fmin(this->global_count_since_top1_percent_ewma5, 0) - count_total;
    }
    else
    {
        this->global_count_since_top1_percent_ewma5 =
            fmax(this->global_count_since_top1_percent_ewma5, 0) + count_total;
    }

    if (this->rank_perc <= 0.50f)
    {
        this->global_count_since_top50_percent_ewma5 =
            fmin(this->global_count_since_top50_percent_ewma5, 0) - count_total;
    }
    else
    {
        this->global_count_since_top50_percent_ewma5 =
            fmax(this->global_count_since_top50_percent_ewma5, 0) + count_total;
    }
#endif
}

void page_info::update_can_promote(size_t num_dram_pages, size_t rank)
{
    (void)num_dram_pages;
    (void)rank;
#if USE_MODEL == (false)
    if (rank < num_dram_pages)
    {
        if (this->score > 0)
        {
            this->hot_age++;
            if (this->hot_age > 1 && (this->score >= this->prev_score))
            {
                this->can_promote = true;
            }
        }
    }
    else
    {
        this->hot_age = 0;
        this->can_promote = false;
    }
#endif
}

float page_info::compute_score(const float *bias)
{
    // Update the score (average of the window)
    // TODO: The model inference goes here
    // Leave this code here. It will be useful for guardrails
    float score = 0;
    for (int i = 0; i < WINDOW_SIZE; i++)
    {
        score += this->w[i] * bias[i];
    }
    return score;
}