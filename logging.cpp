#include "logging.h"
#include <atomic>
#include <fstream>
#include <iomanip>
#include <numa.h>
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

    return static_cast<float>(pid_diff);
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
    PRINT_CELL_AUTO(ewma_var_2);
    PRINT_CELL_AUTO(ewma_var_5);
    PRINT_CELL_AUTO(ewma_var_20);
    PRINT_CELL_AUTO(ewma_var_100);
    PRINT_CELL_AUTO(ewma_100_malloc_perc);
    PRINT_CELL_AUTO(global_count_since_top1_percent_ewma5);
    PRINT_CELL_AUTO(global_count_since_top50_percent_ewma5);

    // Group EWMA5 percentages are always present
    int pm_offset = 7;
    for (int offset = -pm_offset; offset <= pm_offset; offset++)
    {
        char group_header[64];
        snprintf(group_header, sizeof(group_header), "group_%d_mean_ewma5_perc", offset);
        print_cell(os, row->group_ewma5_perc[offset + pm_offset], group_header, header);

        snprintf(group_header, sizeof(group_header), "group_%d_mean_perc", offset);
        print_cell(os, row->groups_perc[offset + pm_offset], group_header, header);

        snprintf(group_header, sizeof(group_header), "group_%d_malloc_calls", offset);
        print_cell(os, row->group_malloc_calls[offset + pm_offset], group_header, header);

        snprintf(group_header, sizeof(group_header), "group_%d_malloc_calls_perc", offset);
        print_cell(os, row->group_malloc_calls_perc[offset + pm_offset], group_header, header);

        snprintf(group_header, sizeof(group_header), "group_%d_malloc_calls_ewma100_perc", offset);
        print_cell(os, row->group_malloc_calls_ewma100_perc[offset + pm_offset], group_header, header);
    }

    PRINT_CELL_AUTO(group_ewma5_var);

    PRINT_CELL_AUTO(age_count_total);

#if FULL_LOGS
    PRINT_CELL_AUTO(malloc_size);
    PRINT_CELL_AUTO(prot);
    PRINT_CELL_AUTO(flags);

    PRINT_CELL_AUTO(ewma_2);
    PRINT_CELL_AUTO(ewma_2_r);
    PRINT_CELL_AUTO(ewma_2_w);
    PRINT_CELL_AUTO(ewma_2_malloc_size);
    PRINT_CELL_AUTO(ewma_2_malloc_calls);

    PRINT_CELL_AUTO(ewma_5);
    PRINT_CELL_AUTO(ewma_5_r);
    PRINT_CELL_AUTO(ewma_5_w);
    PRINT_CELL_AUTO(ewma_5_malloc_size);
    PRINT_CELL_AUTO(ewma_5_malloc_calls);

    PRINT_CELL_AUTO(ewma_20);
    PRINT_CELL_AUTO(ewma_20_r);
    PRINT_CELL_AUTO(ewma_20_w);
    PRINT_CELL_AUTO(ewma_20_malloc_size);
    PRINT_CELL_AUTO(ewma_20_malloc_calls);

    PRINT_CELL_AUTO(ewma_100);
    PRINT_CELL_AUTO(ewma_100_r);
    PRINT_CELL_AUTO(ewma_100_w);
    PRINT_CELL_AUTO(ewma_100_malloc_size);
    PRINT_CELL_AUTO(ewma_100_malloc_calls);

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
    PRINT_CELL_AUTO(read_syscalls);
    PRINT_CELL_AUTO(write_syscalls);
    PRINT_CELL_AUTO(read_bytes);
    PRINT_CELL_AUTO(write_bytes);
    PRINT_CELL_AUTO(sum_malloc_bytes);
    PRINT_CELL_AUTO(min_malloc_bytes);
    PRINT_CELL_AUTO(max_malloc_bytes);
    PRINT_CELL_AUTO(malloc_call);
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

        std::cout << "[ARMS] Writing training data log to output.txt..." << std::endl;
        std::vector<char> buffer(LOG_BUFFER_BYTES);

        std::ofstream ofs;
        std::filebuf *file_buf = ofs.rdbuf();
        file_buf->pubsetbuf(buffer.data(), buffer.size());
        ofs.open("/users/zimooo2/tiering_solutions/output.log");
        if (!ofs.is_open())
        {
            perror("open output.log");
            return;
        }
        std::ostream &os = ofs;

        print_row(os, scores_log, true); // print header
        for (size_t i = 0; (i < logged_samples) && i < MAX_LOGGED_SAMPLES; i++)
        {
            print_row(os, &scores_log[i], false);
        }
        std::cout << "[ARMS] Training data log written to output.txt." << std::endl;
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
    row.global_avg_accesses = static_cast<float>(page->global_avg_accesses);
    row.global_avg_accesses_perc = static_cast<float>(page->global_avg_accesses_perc);

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
    row.ewma_var_2 = page->w_var[0];
    row.ewma_var_5 = page->w_var[1];
    row.ewma_var_20 = page->w_var[2];
    row.ewma_var_100 = page->w_var[3];
    row.ewma_100_malloc_perc = page->malloc_call_perc_ewma[3];

    row.global_count_since_top1_percent_ewma5 = static_cast<float>(page->global_count_since_top1_percent_ewma5);
    row.global_count_since_top50_percent_ewma5 = static_cast<float>(page->global_count_since_top50_percent_ewma5);

    for (int8_t i = -7; i <= 7; ++i)
    {
        struct page_group *pg = try_get_group(grp_tracker, page->va, i);
#if FULL_LOGS
        row.groups[i + 7] = pg != NULL ? pg->avg : 0.0;
        row.group_ewma5[i + 7] = pg != NULL ? pg->avg_ewma5 : 0.0;
#endif
        row.groups_perc[i + 7] = pg != NULL ? pg->avg_perc : 0.0;
        row.group_ewma5_perc[i + 7] = pg != NULL ? pg->avg_perc_ewma5 : 0.0;
        row.group_malloc_calls[i + 7] = pg != NULL ? pg->malloc_calls_avg : 0.0;
        row.group_malloc_calls_perc[i + 7] = pg != NULL ? pg->malloc_calls_avg_perc : 0.0;
        row.group_malloc_calls_ewma100_perc[i + 7] = pg != NULL ? pg->malloc_calls_ewma100_perc : 0.0;
        if (i == 0)
        {
            row.group_ewma5_var = pg != NULL ? pg->var_ewma5 : 0.0;
        }
    }

    row.age_count_total = page->age_count_total;

#if FULL_LOGS == (true)
    row.malloc_size = 0;
    row.prot = page->prot;
    row.flags = page->flags;

    row.ewma_2 = page->w[0];
    row.ewma_2_r = page->w_r[0];
    row.ewma_2_w = page->w_w[0];
    row.ewma_2_malloc_size = page->malloc_size_ewma[0];
    row.ewma_2_malloc_calls = page->malloc_call_ewma[0];

    row.ewma_5 = page->w[1];
    row.ewma_5_r = page->w_r[1];
    row.ewma_5_w = page->w_w[1];
    row.ewma_5_malloc_size = page->malloc_size_ewma[1];
    row.ewma_5_malloc_calls = page->malloc_call_ewma[1];

    row.ewma_20 = page->w[2];
    row.ewma_20_r = page->w_r[2];
    row.ewma_20_w = page->w_w[2];
    row.ewma_20_malloc_size = page->malloc_size_ewma[2];
    row.ewma_20_malloc_calls = page->malloc_call_ewma[2];

    row.ewma_100 = page->w[3];
    row.ewma_100_r = page->w_r[3];
    row.ewma_100_w = page->w_w[3];
    row.ewma_100_malloc_size = page->malloc_size_ewma[3];
    row.ewma_100_malloc_calls = page->malloc_call_ewma[3];

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
    row.read_syscalls = page->read_syscalls;
    row.write_syscalls = page->write_syscalls;
    row.read_bytes = page->read_bytes;
    row.write_bytes = page->write_bytes;
    row.sum_malloc_bytes = page->sum_malloc_bytes;
    row.min_malloc_bytes = page->min_malloc_bytes;
    row.max_malloc_bytes = page->max_malloc_bytes;
    row.malloc_call = page->malloc_call;
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