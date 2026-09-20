// Standalone test: section GC discards unrelated tiering/runtime entry points.
#include "../defs.h"
#ifdef TEST_FULL_LOGS
#undef FULL_LOGS
#define FULL_LOGS true
#endif
#include "../logging.cpp"
#include "../logging_parquet.cpp"
#include <cassert>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>

template <typename T>
void reference_cell(std::ostream &os, T value, const char *name, bool header)
{
    if (header) os << name; else os << value;
    os << ',';
}
template <typename T>
void reference_group(std::ostream &os, const T (&values)[15], const char *suffix, bool header)
{
    for (int i = 0; i < 15; ++i)
    {
        const auto name = "group_" + std::to_string(i - 7) + "_" + suffix;
        reference_cell(os, values[i], name.c_str(), header);
    }
}
#define PRINT_CELL_AUTO(var) reference_cell(os, row->var, #var, header)

void reference_row(std::ostream &os, const struct data_row *row, bool header)
{
    // Base fields (always present)
    PRINT_CELL_AUTO(step);
    PRINT_CELL_AUTO(page);
    PRINT_CELL_AUTO(read);
    PRINT_CELL_AUTO(write);
    PRINT_CELL_AUTO(count);
    PRINT_CELL_AUTO(global_avg_accesses);
    PRINT_CELL_AUTO(global_avg_accesses_model);
    PRINT_CELL_AUTO(ewma_2);
    PRINT_CELL_AUTO(ewma_2_r);
    PRINT_CELL_AUTO(ewma_2_w);
    PRINT_CELL_AUTO(ewma_5);
    PRINT_CELL_AUTO(ewma_5_r);
    PRINT_CELL_AUTO(ewma_5_w);
    PRINT_CELL_AUTO(ewma_20);
    PRINT_CELL_AUTO(ewma_20_r);
    PRINT_CELL_AUTO(ewma_20_w);
    PRINT_CELL_AUTO(ewma_100);
    PRINT_CELL_AUTO(ewma_100_r);
    PRINT_CELL_AUTO(ewma_100_w);
    PRINT_CELL_AUTO(virtual_missed_ewma_100);
    PRINT_CELL_AUTO(gap4);
    PRINT_CELL_AUTO(read_write_gap3);
    PRINT_CELL_AUTO(ewma_var_2);
    PRINT_CELL_AUTO(ewma_var_5);
    PRINT_CELL_AUTO(ewma_var_20);
    PRINT_CELL_AUTO(ewma_var_100);

#if FULL_LOGS
    PRINT_CELL_AUTO(global_count_since_top1_percent_ewma5);
    PRINT_CELL_AUTO(global_count_since_top50_percent_ewma5);
#endif

    // Group EWMA5 percentages are always present
    reference_group(os, row->group_ewma5, "mean_ewma5", header);
    reference_group(os, row->groups, "mean", header);

    PRINT_CELL_AUTO(group_ewma5_var);

    PRINT_CELL_AUTO(age_count_total);

#if FULL_LOGS
    PRINT_CELL_AUTO(prot);
    PRINT_CELL_AUTO(flags);

    PRINT_CELL_AUTO(ewma_2_abs);
    PRINT_CELL_AUTO(ewma_2_r_abs);
    PRINT_CELL_AUTO(ewma_2_w_abs);

    PRINT_CELL_AUTO(ewma_5_abs);
    PRINT_CELL_AUTO(ewma_5_r_abs);
    PRINT_CELL_AUTO(ewma_5_w_abs);

    PRINT_CELL_AUTO(ewma_20_abs);
    PRINT_CELL_AUTO(ewma_20_r_abs);
    PRINT_CELL_AUTO(ewma_20_w_abs);

    PRINT_CELL_AUTO(ewma_100_abs);
    PRINT_CELL_AUTO(ewma_100_r_abs);
    PRINT_CELL_AUTO(ewma_100_w_abs);

    PRINT_CELL_AUTO(rank);
    PRINT_CELL_AUTO(rank_perc);
    PRINT_CELL_AUTO(rank_ewma_2);
    PRINT_CELL_AUTO(rank_ewma_5);
    PRINT_CELL_AUTO(rank_ewma_20);
    PRINT_CELL_AUTO(rank_ewma_100);

    PRINT_CELL_AUTO(count_total);
    PRINT_CELL_AUTO(global_count_similar);
    PRINT_CELL_AUTO(diff);
    PRINT_CELL_AUTO(cpu_usage);
    PRINT_CELL_AUTO(disk_read_bytes);
    PRINT_CELL_AUTO(disk_write_bytes);
    PRINT_CELL_AUTO(syscr);
    PRINT_CELL_AUTO(syscw);

    reference_group(os, row->groups_abs, "mean_abs", header);
    reference_group(os, row->group_ewma5_abs, "mean_ewma5_abs", header);

    PRINT_CELL_AUTO(model_selection);
    PRINT_CELL_AUTO(read_bytes);
    PRINT_CELL_AUTO(write_bytes);
#endif

    PRINT_CELL_AUTO(model_score);
    PRINT_CELL_AUTO(arms_score);
    PRINT_CELL_AUTO(score);
    PRINT_CELL_AUTO(in_dram);

    PRINT_CELL_AUTO(age);

    PRINT_CELL_AUTO(num_demotions);
    PRINT_CELL_AUTO(num_promotions);

    PRINT_CELL_AUTO(discounted_reward_90);
    PRINT_CELL_AUTO(discounted_reward_95);
    PRINT_CELL_AUTO(discounted_reward_99);
    PRINT_CELL_AUTO(step_unc_m_cas_count_wr);
#if MODEL_TIMING_TELEMETRY_ENABLED
    PRINT_CELL_AUTO(model_feature_aggregation_ns);
    PRINT_CELL_AUTO(model_inference_ns);
    PRINT_CELL_AUTO(model_score_total_ns);
#endif
#if ARMS_TIMING_TELEMETRY_ENABLED
    PRINT_CELL_AUTO(arms_feature_aggregation_ns);
    PRINT_CELL_AUTO(arms_scoring_ns);
    PRINT_CELL_AUTO(arms_score_total_ns);
#endif

    os << '\n';
}

#undef PRINT_CELL_AUTO

using AccessLog = class access_log;
class Fixture : public AccessLog
{
  public:
    void fill(size_t count)
    {
        logged_samples = count == 0 ? -1 : count; // also test skipped/empty samples
        for (size_t i = 0; i < count; ++i)
        {
            data_row &r = scores_log[i];
            r = {};
            r.step = i;
            r.page = std::numeric_limits<size_t>::max() - i;
            r.count = i % 19;
            r.read = i % 7;
            r.write = i % 11;
            r.ewma_2 = i == 0 ? -0.0f : 1.0f / static_cast<float>(i);
            r.ewma_5 = std::numeric_limits<float>::quiet_NaN();
            r.ewma_20 = std::numeric_limits<float>::infinity();
            r.ewma_100 = -std::numeric_limits<float>::infinity();
            r.score = std::numeric_limits<float>::denorm_min();
            r.global_avg_accesses = std::numeric_limits<float>::max();
            r.in_dram = i % 2;
            r.num_demotions = -3;
            r.num_promotions = 4;
            r.age = i;
            r.age_count_total = std::numeric_limits<uint64_t>::max() - i;
            r.step_unc_m_cas_count_wr = std::numeric_limits<uint64_t>::max();
#if MODEL_TIMING_TELEMETRY_ENABLED
            r.model_feature_aggregation_ns = 123456789123456789ULL;
            r.model_inference_ns = 987654321ULL;
            r.model_score_total_ns = r.model_feature_aggregation_ns + r.model_inference_ns + i;
#endif
#if ARMS_TIMING_TELEMETRY_ENABLED
            r.arms_feature_aggregation_ns = 123456789123456789ULL;
            r.arms_scoring_ns = 987654321ULL;
            r.arms_score_total_ns = r.arms_feature_aggregation_ns + r.arms_scoring_ns;
#endif
            for (int j = 0; j < 15; ++j)
            {
                r.groups[j] = (j - 7) * 0.3f;
                r.group_ewma5[j] = (j + 1) * 0.7f;
#if FULL_LOGS
                r.groups_abs[j] = j * 0.2f;
                r.group_ewma5_abs[j] = -j * 0.2f;
#endif
            }
#if FULL_LOGS
            r.disk_read_bytes = -11;
            r.disk_write_bytes = 123456789123456789LL;
            r.prot = 5;
            r.flags = 2;
#endif
        }
        if (count > 1)
            scores_log[1].prev = &scores_log[0];
    }

    void reference(const std::string &path, size_t count)
    {
        std::ofstream out(path);
        out << std::setprecision(17);
        const data_row empty{};
        reference_row(out, &empty, true);
        for (size_t i = 0; i < count; ++i)
            if (i < 2 || i == 65535 || i + 1 == count)
                reference_row(out, &scores_log[i], false);
    }
};

int main(int argc, char **argv)
{
    assert(argc == 4);
    const size_t count = std::stoull(argv[2]);
    assert(count <= MAX_LOGGED_SAMPLES);
    Fixture log;
    log.fill(count);
    setenv("LOG_OUTPUT_PATH", argv[1], 1);
    log.pebs_write_log();
    log.pebs_write_log(); // must not duplicate rows or overwrite twice
    log.reference(argv[3], count);
}
