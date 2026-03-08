#include "logging.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <numa.h>
#include <string>
#include <thread>
#include <vector>

extern bool terminated; // defined in arms_kernel.cpp

/* Define globals declared in logging.h here to provide a single
    definition for the linker. This prevents multiple-definition errors
    when multiple .c files include logging.h. */
struct access_log *access_log;

namespace
{
constexpr int LOG_NUMA_NODE = 1;
}

access_log::access_log() : logged_samples(0), scores_log(nullptr)
{
    if (PRINT_TRAINING_DATA)
    {
        const size_t log_bytes = sizeof(struct data_row) * MAX_LOGGED_SAMPLES;

        if (numa_available() >= 0)
        {
            void *log_mem = numa_alloc_onnode(log_bytes, LOG_NUMA_NODE);
            if (log_mem != nullptr)
            {
                scores_log = static_cast<struct data_row *>(log_mem);
            }
        }

        if (scores_log == nullptr)
        {
            scores_log = new struct data_row[MAX_LOGGED_SAMPLES];
        }

        memset(scores_log, 0, log_bytes);

        if (numa_available() >= 0)
        {
            numa_tonode_memory(scores_log, log_bytes, LOG_NUMA_NODE);
        }
    }
}

float access_log::get_gb_allocated()
{
    if (!PRINT_TRAINING_DATA)
    {
        return 0.0f;
    }
    return (float)(MAX_LOGGED_SAMPLES * sizeof(struct data_row)) / (1024 * 1024 * 1024);
}

int access_log::get_disk_usage(const pid_t pid)
{
    prev_disk_stat = curr_disk_stat;
    // convert  pid to string
    char stat_filepath[50];
    snprintf(stat_filepath, sizeof(stat_filepath), "/proc/%d/io", pid);

    FILE *fpstat = fopen(stat_filepath, "r");
    if (fpstat == NULL)
    {
        perror("FOPEN ERROR ");
        return -1;
    }

    char line[256];
    while (fgets(line, sizeof(line), fpstat) != NULL)
    {
        if (strncmp(line, "rchar:", 6) == 0)
        {
            sscanf(line, "rchar: %lld", &(curr_disk_stat.rchar));
        }
        else if (strncmp(line, "wchar:", 6) == 0)
        {
            sscanf(line, "wchar: %lld", &(curr_disk_stat.wchar));
        }
        else if (strncmp(line, "syscr:", 6) == 0)
        {
            sscanf(line, "syscr: %lld", &(curr_disk_stat.syscr));
        }
        else if (strncmp(line, "syscw:", 6) == 0)
        {
            sscanf(line, "syscw: %lld", &(curr_disk_stat.syscw));
        }
        else if (strncmp(line, "read_bytes:", 6) == 0)
        {
            sscanf(line, "read_bytes: %lld", &(curr_disk_stat.read_bytes));
        }
        else if (strncmp(line, "write_bytes:", 6) == 0)
        {
            sscanf(line, "write_bytes: %lld", &(curr_disk_stat.write_bytes));
        }
    }

    fclose(fpstat);
    return 0;
}

int access_log::get_cpu_usage(const pid_t pid)
{
    prev_cpu_stat = curr_cpu_stat;

    // convert  pid to string
    char stat_filepath[50];
    snprintf(stat_filepath, sizeof(stat_filepath), "/proc/%d/stat", pid);

    FILE *fpstat = fopen(stat_filepath, "r");
    if (fpstat == NULL)
    {
        perror("FOPEN ERROR ");
        return -1;
    }

    // read values from /proc/pid/stat
    memset(&curr_cpu_stat, 0, sizeof(struct cpu_stat));
    long int rss;
    if (fscanf(fpstat,
               "%*d %*s %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu"
               "%lu %ld %ld %*d %*d %*d %*d %*u %lu %ld",
               &curr_cpu_stat.utime_ticks, &curr_cpu_stat.stime_ticks, &curr_cpu_stat.cutime_ticks,
               &curr_cpu_stat.cstime_ticks, &curr_cpu_stat.vsize, &rss) == EOF)
    {
        fclose(fpstat);
        return -1;
    }
    fclose(fpstat);
    curr_cpu_stat.rss = rss * getpagesize();

    return 0;
}

float access_log::calc_cpu_usage_pct()
{
    const long unsigned int pid_diff = (curr_cpu_stat.utime_ticks + curr_cpu_stat.stime_ticks) -
                                       (prev_cpu_stat.utime_ticks + prev_cpu_stat.stime_ticks);

    return 1 / (float)1 * pid_diff;
}

void access_log::update_proc_stats()
{
    static int pid = 0;
    if (pid == 0)
    {
        pid = getpid();
    }
    else
    {
        prev_cpu_stat = curr_cpu_stat;
        prev_disk_stat = curr_disk_stat;
    }
    if (get_cpu_usage(pid) == -1)
    {
        printf("error\n");
    }
    if (get_disk_usage(pid) == -1)
    {
        printf("error\n");
    }
}

namespace
{
constexpr std::size_t LOG_BUFFER_BYTES = 100ULL * 1024ULL * 1024ULL;
constexpr std::size_t LOG_FILE_COUNT = 10;
constexpr std::size_t LOG_PARTS_PER_FILE = 10;
constexpr const char *DEFAULT_LOG_OUTPUT_PATH = "/home/freischuetz/memory_tiering/tiering_solutions/output.log";

std::string build_log_output_base_path()
{
    const char *env_path = std::getenv("LOG_OUTPUT_PATH");
    std::string output_path = (env_path != nullptr && env_path[0] != '\0') ? env_path : DEFAULT_LOG_OUTPUT_PATH;

    if (output_path.size() >= 4 && output_path.compare(output_path.size() - 4, 4, ".log") == 0)
    {
        output_path.erase(output_path.size() - 4);
    }

    return output_path;
}

inline bool ranges_overlap(uint64_t a_start, uint64_t a_end, uint64_t b_start, uint64_t b_end)
{
    return a_start < b_end && b_start < a_end;
}

template <typename T> inline void print_cell(std::ostream &os, const T &value, const char *header, bool print_header)
{
    if (print_header)
    {
        os << header;
    }
    else
    {
        os << value;
    }
    os << ',';
}
} // namespace

#define PRINT_CELL_AUTO(var) print_cell(os, row->var, #var, header)

void access_log::print_row(std::ostream &os, struct data_row *row, bool header)
{
    // Keep floating-point output consistent with previous fprintf %f usage.
    auto original_flags = os.flags();
    auto original_precision = os.precision();
    os << std::fixed << std::setprecision(6);

    // Base fields (always present)
    PRINT_CELL_AUTO(step);
    PRINT_CELL_AUTO(page);
    PRINT_CELL_AUTO(read);
    PRINT_CELL_AUTO(write);
    PRINT_CELL_AUTO(count);
    PRINT_CELL_AUTO(global_avg_accesses);
    PRINT_CELL_AUTO(global_avg_accesses_perc);
    PRINT_CELL_AUTO(ewma_2_perc);
    PRINT_CELL_AUTO(ewma_2_r_perc);
    PRINT_CELL_AUTO(ewma_2_w_perc);
    PRINT_CELL_AUTO(ewma_5_perc);
    PRINT_CELL_AUTO(ewma_5_r_perc);
    PRINT_CELL_AUTO(ewma_5_w_perc);
    PRINT_CELL_AUTO(ewma_20_perc);
    PRINT_CELL_AUTO(ewma_20_r_perc);
    PRINT_CELL_AUTO(ewma_20_w_perc);
    PRINT_CELL_AUTO(ewma_100_perc);
    PRINT_CELL_AUTO(ewma_100_r_perc);
    PRINT_CELL_AUTO(ewma_100_w_perc);
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
    int pm_offset = 7;
    for (int offset = -pm_offset; offset <= pm_offset; offset++)
    {
        char group_header[64];
        snprintf(group_header, sizeof(group_header), "group_%d_mean_ewma5_perc", offset);
        print_cell(os, row->group_ewma5_perc[offset + pm_offset], group_header, header);

        snprintf(group_header, sizeof(group_header), "group_%d_mean_perc", offset);
        print_cell(os, row->groups_perc[offset + pm_offset], group_header, header);
    }

    PRINT_CELL_AUTO(group_ewma5_var);

    PRINT_CELL_AUTO(age_count_total);

#if FULL_LOGS
    PRINT_CELL_AUTO(prot);
    PRINT_CELL_AUTO(flags);

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

    for (int offset = -pm_offset; offset <= pm_offset; offset++)
    {
        char group_header[64];
        snprintf(group_header, sizeof(group_header), "group_%d_mean", offset);
        print_cell(os, row->groups[offset + pm_offset], group_header, header);

        snprintf(group_header, sizeof(group_header), "group_%d_mean_ewma5", offset);
        print_cell(os, row->group_ewma5[offset + pm_offset], group_header, header);
    }

    PRINT_CELL_AUTO(model_selection);
    PRINT_CELL_AUTO(read_bytes);
    PRINT_CELL_AUTO(write_bytes);
#endif

    PRINT_CELL_AUTO(model_score);
    PRINT_CELL_AUTO(arms_score);
    PRINT_CELL_AUTO(score);
    PRINT_CELL_AUTO(pages_in_dram);
    PRINT_CELL_AUTO(seen_pages);

    PRINT_CELL_AUTO(age);

    PRINT_CELL_AUTO(num_demotions);
    PRINT_CELL_AUTO(num_promotions);

    PRINT_CELL_AUTO(discounted_reward_90);
    PRINT_CELL_AUTO(discounted_reward_95);
    PRINT_CELL_AUTO(discounted_reward_99);

    os << "\n";

    os.flags(original_flags);
    os.precision(original_precision);
}

void access_log::finalize_log()
{
    // Clamp to the last valid index to avoid walking past scores_log when
    // logged_samples == MAX_LOGGED_SAMPLES (or higher due to races).
    const size_t sample_count = std::min(logged_samples, static_cast<size_t>(MAX_LOGGED_SAMPLES));
    if (sample_count == 0)
    {
        return;
    }

    for (int64_t i = static_cast<int64_t>(sample_count) - 1; i >= 0; i--)
    {
        struct data_row *current_row = &scores_log[i];

        if (current_row->prev != nullptr)
        {
            current_row->prev->discounted_reward_90 = current_row->count + 0.9f * current_row->discounted_reward_90;
            current_row->prev->discounted_reward_95 = current_row->count + 0.95f * current_row->discounted_reward_95;
            current_row->prev->discounted_reward_99 = current_row->count + 0.99f * current_row->discounted_reward_99;

            // penalty for pages that got deallocated and reallocated (this shouldn't really happen often)
            int age_diff = current_row->age - current_row->prev->age;
            for (int a = age_diff - 1; a > 0; a--)
            {
                current_row->prev->discounted_reward_90 *= 0.9f;
                current_row->prev->discounted_reward_95 *= 0.95f;
                current_row->prev->discounted_reward_99 *= 0.99f;
            }
        }
    }
}

void build_model()
{
}

void access_log::pebs_write_log()
{
    if (PRINT_TRAINING_DATA)
    {
        static bool prev_called = false;
        if (prev_called)
        {
            return;
        }
        prev_called = true;

        std::cout << "[ARMS] Finalizing training data log..." << std::endl;
        finalize_log();

        std::cout << "[ARMS] Build Model..." << std::endl;
        build_model();

        std::cout << "[ARMS] Writing training data log into 10 files (10 threaded parts each)..." << std::endl;

        const size_t sample_count = std::min(logged_samples, static_cast<size_t>(MAX_LOGGED_SAMPLES));
        const std::string output_base = build_log_output_base_path();
        const std::filesystem::path output_base_path(output_base);
        const std::filesystem::path output_dir = output_base_path.parent_path();
        if (!output_dir.empty())
        {
            std::error_code ec;
            std::filesystem::create_directories(output_dir, ec);
        }

        const size_t total_parts = LOG_FILE_COUNT * LOG_PARTS_PER_FILE;
        std::vector<size_t> boundaries(total_parts + 1, 0);
        for (size_t i = 0; i <= total_parts; ++i)
        {
            boundaries[i] = (sample_count * i) / total_parts;
        }

        auto write_part = [this](const std::string &part_path, size_t begin_idx, size_t end_idx) -> bool {
            std::vector<char> part_buffer(LOG_BUFFER_BYTES);
            std::ofstream part_ofs;
            std::filebuf *part_buf = part_ofs.rdbuf();
            part_buf->pubsetbuf(part_buffer.data(), part_buffer.size());
            part_ofs.open(part_path);
            if (!part_ofs.is_open())
            {
                return false;
            }

            for (size_t i = begin_idx; i < end_idx; ++i)
            {
                print_row(part_ofs, &scores_log[i], false);
            }
            return true;
        };

        std::atomic<bool> write_failed{false};

        auto write_file_group = [&](size_t file_idx) {
            if (write_failed.load())
            {
                return;
            }

            {
                std::vector<std::thread> threads;
                std::vector<char> part_ok(LOG_PARTS_PER_FILE, 1);
                std::vector<std::string> part_paths(LOG_PARTS_PER_FILE);
                threads.reserve(LOG_PARTS_PER_FILE);

                for (size_t part_idx = 0; part_idx < LOG_PARTS_PER_FILE; ++part_idx)
                {
                    const size_t global_part_idx = file_idx * LOG_PARTS_PER_FILE + part_idx;
                    const size_t begin_idx = boundaries[global_part_idx];
                    const size_t end_idx = boundaries[global_part_idx + 1];
                    part_paths[part_idx] = output_base + "_" + std::to_string(file_idx) + "_part_" +
                                           std::to_string(part_idx) + "_" + std::to_string(getpid()) + ".tmp";

                    threads.emplace_back([&, part_idx, begin_idx, end_idx] {
                        part_ok[part_idx] = write_part(part_paths[part_idx], begin_idx, end_idx) ? 1 : 0;
                    });
                }

                for (auto &th : threads)
                {
                    th.join();
                }

                std::vector<char> final_buffer(LOG_BUFFER_BYTES);
                const std::string final_path = output_base + "_" + std::to_string(file_idx) + ".log";
                std::ofstream final_ofs;
                std::filebuf *final_buf = final_ofs.rdbuf();
                final_buf->pubsetbuf(final_buffer.data(), final_buffer.size());
                final_ofs.open(final_path);
                if (!final_ofs.is_open())
                {
                    perror("open final output log");
                    write_failed.store(true);
                    return;
                }

                print_row(final_ofs, scores_log, true);
                for (size_t part_idx = 0; part_idx < LOG_PARTS_PER_FILE; ++part_idx)
                {
                    if (!part_ok[part_idx])
                    {
                        continue;
                    }

                    std::ifstream part_ifs(part_paths[part_idx]);
                    if (part_ifs.is_open())
                    {
                        final_ofs << part_ifs.rdbuf();
                        part_ifs.close();
                    }
                    std::remove(part_paths[part_idx].c_str());
                }
            }
        };

        std::vector<std::thread> file_group_threads;
        file_group_threads.reserve(LOG_FILE_COUNT);
        for (size_t file_idx = 0; file_idx < LOG_FILE_COUNT; ++file_idx)
        {
            file_group_threads.emplace_back([&, file_idx] { write_file_group(file_idx); });
        }

        for (auto &th : file_group_threads)
        {
            th.join();
        }

        if (write_failed.load())
        {
            std::cout << "[ARMS] Failed writing one or more output log files." << std::endl;
            return;
        }

        std::cout << "[ARMS] Training data log written to " << output_base << "_0.log ... " << output_base << "_9.log."
                  << std::endl;
    }
}

struct data_row access_log::extract_row(size_t step, const std::shared_ptr<page_info> &page,
                                        struct group_tracker *grp_tracker, size_t count_all_pages)
{
    struct data_row row{};

#if FULL_LOGS
    float cpu_usage = calc_cpu_usage_pct();
#else
    (void)count_all_pages;
#endif

    // Base fields (always present)
    row.step = step;
    row.page = page->va;
    row.read = page->reads;
    row.write = page->writes;
    row.count = page->count;
    row.global_avg_accesses = page->global_avg_accesses;
    row.global_avg_accesses_perc = page->global_avg_accesses_perc;

    row.ewma_2_perc = page->w_perc[0];
    row.ewma_2_r_perc = page->w_r_perc[0];
    row.ewma_2_w_perc = page->w_w_perc[0];
    row.ewma_5_perc = page->w_perc[1];
    row.ewma_5_r_perc = page->w_r_perc[1];
    row.ewma_5_w_perc = page->w_w_perc[1];
    row.ewma_20_perc = page->w_perc[2];
    row.ewma_20_r_perc = page->w_r_perc[2];
    row.ewma_20_w_perc = page->w_w_perc[2];
    row.ewma_100_perc = page->w_perc[3];
    row.ewma_100_r_perc = page->w_r_perc[3];
    row.ewma_100_w_perc = page->w_w_perc[3];
    row.gap4 = page->gap4;
    row.read_write_gap3 = page->read_write_gap3;
    row.ewma_var_2 = page->w_perc_var[0];
    row.ewma_var_5 = page->w_perc_var[1];
    row.ewma_var_20 = page->w_perc_var[2];
    row.ewma_var_100 = page->w_perc_var[3];

#if FULL_LOGS
    row.global_count_since_top1_percent_ewma5 = page->global_count_since_top1_percent_ewma5;
    row.global_count_since_top50_percent_ewma5 = page->global_count_since_top50_percent_ewma5;
#endif

    struct page_group *group_window[15] = {0};
    get_group_window(grp_tracker, page->va, group_window);
    for (int8_t i = -7; i <= 7; ++i)
    {
        struct page_group *pg = group_window[i + 7];
#if FULL_LOGS
        row.groups[i + 7] = pg != NULL ? pg->avg : 0.0;
        row.group_ewma5[i + 7] = pg != NULL ? pg->avg_ewma5 : 0.0;
#endif
        row.groups_perc[i + 7] = pg != NULL ? pg->avg_perc : 0.0;
        row.group_ewma5_perc[i + 7] = pg != NULL ? pg->avg_perc_ewma5 : 0.0;
    }

    // Variance of group ewma5 percentages across neighbor groups
    {
        float sum = 0.0f;
        float sum_sq = 0.0f;
        const int count = 15;
        for (int idx = 0; idx < count; ++idx)
        {
            const float v = row.group_ewma5_perc[idx];
            sum += v;
            sum_sq += v * v;
        }
        const float mean = sum / (float)count;
        const float var = (sum_sq / (float)count) - (mean * mean);
        row.group_ewma5_var = var > 0.0f ? var : 0.0f;
    }

    row.age_count_total = page->age_count_total;

#if FULL_LOGS == (true)
    row.prot = page->prot;
    row.flags = page->flags;

    row.ewma_2 = page->w[0];
    row.ewma_2_r = page->w_r[0];
    row.ewma_2_w = page->w_w[0];

    row.ewma_5 = page->w[1];
    row.ewma_5_r = page->w_r[1];
    row.ewma_5_w = page->w_w[1];

    row.ewma_20 = page->w[2];
    row.ewma_20_r = page->w_r[2];
    row.ewma_20_w = page->w_w[2];

    row.ewma_100 = page->w[3];
    row.ewma_100_r = page->w_r[3];
    row.ewma_100_w = page->w_w[3];

    row.rank = page->rank;
    row.rank_perc = page->rank_perc;
    row.rank_ewma_2 = page->rank_perc_ewma[0];
    row.rank_ewma_5 = page->rank_perc_ewma[1];
    row.rank_ewma_20 = page->rank_perc_ewma[2];
    row.rank_ewma_100 = page->rank_perc_ewma[3];

    row.count_total = count_all_pages;
    row.global_count_similar = page->global_count_similar;
    row.diff = page->diff;
    row.cpu_usage = cpu_usage;
    row.disk_read_bytes = (curr_disk_stat.read_bytes - prev_disk_stat.read_bytes);
    row.disk_write_bytes = (curr_disk_stat.write_bytes - prev_disk_stat.write_bytes);
    row.syscr = (curr_disk_stat.syscr - prev_disk_stat.syscr);
    row.syscw = (curr_disk_stat.syscw - prev_disk_stat.syscw);

    row.model_selection = page->model_selection;
    row.read_bytes = page->read_bytes;
    row.write_bytes = page->write_bytes;
#endif

    row.pages_in_dram = page->pages_in_dram;
    row.seen_pages = page->seen_pages;
    row.age = page->age;

    row.num_demotions = page->num_demotions;
    row.num_promotions = page->num_promotions;
    page->num_demotions = 0;
    page->num_promotions = 0;

    row.discounted_reward_90 = 0.0f;
    row.discounted_reward_95 = 0.0f;
    row.discounted_reward_99 = 0.0f;

    return row;
}

void access_log::log_row(const std::shared_ptr<page_info> &page, struct data_row &row)
{
    if (!PRINT_TRAINING_DATA)
    {
        return;
    }
    if (logged_samples >= MAX_LOGGED_SAMPLES)
    {
        // Cap to avoid spilling past allocated log buffer.
        logged_samples = MAX_LOGGED_SAMPLES;
        terminated = true;
        pebs_write_log();
        exit(0);
        return;
    }

    scores_log[logged_samples] = row;
    struct data_row *assigned_row = &(scores_log[logged_samples]);

    assigned_row->prev = page->last_logged_row;
    page->last_logged_row = assigned_row;

    logged_samples++;
}

bool access_log::overlaps_with_logging_region(uint64_t addr, uint64_t length) const
{
    if (length == 0)
    {
        return false;
    }

    uint64_t range_end = addr + length;
    if (range_end < addr)
    {
        range_end = UINT64_MAX;
    }

    const uint64_t log_start = reinterpret_cast<uint64_t>(this);
    const uint64_t log_end = log_start + sizeof(*this);
    if (ranges_overlap(addr, range_end, log_start, log_end))
    {
        return true;
    }

    if (scores_log != nullptr)
    {
        const uint64_t scores_start = reinterpret_cast<uint64_t>(scores_log);
        const uint64_t scores_end = scores_start + (sizeof(struct data_row) * MAX_LOGGED_SAMPLES);
        if (ranges_overlap(addr, range_end, scores_start, scores_end))
        {
            return true;
        }
    }

    return false;
}