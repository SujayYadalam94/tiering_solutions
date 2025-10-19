#include "logging.h"

/* Define globals declared in logging.h here to provide a single
    definition for the linker. This prevents multiple-definition errors
    when multiple .c files include logging.h. */
struct cpu_stat prev_cpu_stat = {0};
struct cpu_stat curr_cpu_stat = {0};
struct disk_stat prev_disk_stat = {0};
struct disk_stat curr_disk_stat = {0};

size_t logged_samples = 0;
struct data_row *scores_log = NULL;
_Atomic size_t migration_queue_index = 0;
_Atomic size_t migration_queue_size = 0;
_Atomic(struct migration_event *) migration_event_queue;

int get_disk_usage(const pid_t pid) {
    prev_disk_stat = curr_disk_stat;
    // convert  pid to string
    char stat_filepath[50];
    snprintf(stat_filepath, sizeof(stat_filepath), "/proc/%d/io", pid);

    FILE *fpstat = fopen(stat_filepath, "r");
    if (fpstat == NULL) {
        perror("FOPEN ERROR ");
        return -1;
    }

    char line[256];
    while (fgets(line, sizeof(line), fpstat) != NULL) {
        if (strncmp(line, "rchar:", 6) == 0) {
            sscanf(line, "rchar: %lld", &(curr_disk_stat.rchar));
        } else if (strncmp(line, "wchar:", 6) == 0) {
            sscanf(line, "wchar: %lld", &(curr_disk_stat.wchar));
        } else if (strncmp(line, "syscr:", 6) == 0) {
            sscanf(line, "syscr: %lld", &(curr_disk_stat.syscr));
        } else if (strncmp(line, "syscw:", 6) == 0) {
            sscanf(line, "syscw: %lld", &(curr_disk_stat.syscw));
        } else if (strncmp(line, "read_bytes:", 6) == 0) {
            sscanf(line, "read_bytes: %lld", &(curr_disk_stat.read_bytes));
        } else if (strncmp(line, "write_bytes:", 6) == 0) {
            sscanf(line, "write_bytes: %lld", &(curr_disk_stat.write_bytes));
        }
    }

    fclose(fpstat);
    return 0;
}

int get_cpu_usage(const pid_t pid) {
    prev_cpu_stat = curr_cpu_stat;

    // convert  pid to string
    char stat_filepath[50];
    snprintf(stat_filepath, sizeof(stat_filepath), "/proc/%d/stat", pid);

    FILE *fpstat = fopen(stat_filepath, "r");
    if (fpstat == NULL) {
        perror("FOPEN ERROR ");
        return -1;
    }

    //read values from /proc/pid/stat
    bzero(&curr_cpu_stat, sizeof(struct cpu_stat));
    long int rss;
    if (fscanf(fpstat, "%*d %*s %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu"
                "%lu %ld %ld %*d %*d %*d %*d %*u %lu %ld",
                &curr_cpu_stat.utime_ticks, &curr_cpu_stat.stime_ticks,
                &curr_cpu_stat.cutime_ticks, &curr_cpu_stat.cstime_ticks, &curr_cpu_stat.vsize,
                &rss) == EOF) {
        fclose(fpstat);
        return -1;
    }
    fclose(fpstat);
    curr_cpu_stat.rss = rss * getpagesize();

    return 0;
}

double calc_cpu_usage_pct()
{
    const long unsigned int pid_diff = (curr_cpu_stat.utime_ticks + curr_cpu_stat.stime_ticks) -
                                       (prev_cpu_stat.utime_ticks + prev_cpu_stat.stime_ticks);

    return 1/(float)1 * pid_diff;
}


void update_proc_stats(){
    static int pid = 0;
    if (pid == 0)
    {
        pid = getpid();

    }
    else{
        prev_cpu_stat = curr_cpu_stat;
        prev_disk_stat = curr_disk_stat;
    }
    if( get_cpu_usage(pid) == -1 ) {
        printf( "error\n" );
    }
    if (get_disk_usage(pid) == -1) {
        printf("error\n");
    }
}

#define PRINT_CELL(f, value, header, print_header, fmt) \
  do { \
    if (print_header) { \
      fprintf(f, "%s,", header); \
    } else { \
      fprintf(f, fmt ",", value); \
    } \
  } while (0)

#define PRINT_CELL_AUTO(var, fmt) \
    PRINT_CELL(f, row->var, #var, header, fmt)

void print_row(FILE *f, struct data_row *row, bool header) {
    PRINT_CELL_AUTO(step, "%zu");
    PRINT_CELL_AUTO(page, "%zu");
    PRINT_CELL_AUTO(read, "%zu");
    PRINT_CELL_AUTO(write, "%zu");
    PRINT_CELL_AUTO(count, "%zu");
    PRINT_CELL_AUTO(malloc_size, "%zu");
    //print_cell<size_t>(ofs, row->age, "age", header);
    //print_cell<size_t>(ofs, row->hot_age, "hot_age", header);

    int prot = row->prot;
    int flags = row->flags;

    //print_cell<char>(ofs, (prot & PROT_READ) ? '1' : '0', "prot_read", header);
    //print_cell<char>(ofs, (prot & PROT_WRITE)? '1' : '0', "prot_write", header);
    //print_cell<char>(ofs, (prot & PROT_EXEC)? '1' : '0', "prot_exec", header);
    //print_cell<char>(ofs, (prot & PROT_NONE)  ? '1' : '0', "prot_none", header);
    //print_cell<char>(ofs, (prot & PROT_GROWSDOWN) ? '1' : '0', "prot_growdown", header);
    //print_cell<char>(ofs, (prot & PROT_GROWSUP)? '1' : '0', "prot_growsup", header);
//
    //print_cell<char>(ofs, (flags & MAP_SHARED) ? '1' : '0', "flags_shared", header);
    //print_cell<char>(ofs, (flags & MAP_SHARED_VALIDATE) ? '1' : '0', "flags_shared_validate", header);
    //print_cell<char>(ofs, (flags & MAP_PRIVATE) ? '1' : '0', "flags_private", header);
    //print_cell<char>(ofs, (flags & MAP_32BIT ) ? '1' : '0', "flags_32bit", header);
    //print_cell<char>(ofs, (flags & MAP_ANONYMOUS)  ? '1' : '0', "flags_anonymous", header);
    //print_cell<char>(ofs, (flags & MAP_FIXED) ? '1' : '0', "flags_fixed", header);
    //print_cell<char>(ofs, (flags & MAP_FIXED_NOREPLACE) ? '1' : '0', "flags_fixed_noreplace", header);
    //print_cell<char>(ofs, (flags & MAP_GROWSDOWN) ? '1' : '0', "flags_growdown", header);
    //print_cell<char>(ofs, (flags & MAP_HUGETLB)  ? '1' : '0', "flags_hugetlb", header);
    //print_cell<char>(ofs, (flags & MAP_HUGE_2MB) == MAP_HUGE_2MB ? '1' : '0', "flags_huge_2mb", header);
    //print_cell<char>(ofs, (flags & MAP_HUGE_1GB) == MAP_HUGE_1GB ? '1' : '0', "flags_huge_1gb", header);
    //print_cell<char>(ofs, (flags & MAP_LOCKED) ? '1' : '0', "flags_locked", header);
    //print_cell<char>(ofs, (flags & MAP_NONBLOCK)  ? '1' : '0', "flags_nonblock", header);
    //print_cell<char>(ofs, (flags & MAP_NORESERVE)  ? '1' : '0', "flags_noreserve", header);
    //print_cell<char>(ofs, (flags & MAP_POPULATE)  ? '1' : '0', "flags_populate", header);
    //print_cell<char>(ofs, (flags & MAP_STACK) ? '1' : '0', "flags_stack", header);
    //print_cell<char>(ofs, (flags & MAP_SYNC) ? '1' : '0', "flags_sync", header);
    //print_cell<char>(ofs, (flags & MAP_DENYWRITE) ? '1' : '0', "flags_denywrite", header);
    //print_cell<char>(ofs, (flags & MAP_EXECUTABLE) ? '1' : '0', "flags_executable", header);

    //print_cell<size_t>(ofs, page->cumsum_reads, "cumsum_reads", header);
    //print_cell<size_t>(ofs, page->cumsum_writes, "cumsum_writes", header);
    PRINT_CELL_AUTO(ewma_2, "%f");
    PRINT_CELL_AUTO(ewma_5, "%f");
    PRINT_CELL_AUTO(ewma_20, "%f");
    PRINT_CELL_AUTO(ewma_100, "%f");
    PRINT_CELL_AUTO(ewma_2_r, "%f");
    PRINT_CELL_AUTO(ewma_5_r, "%f");
    PRINT_CELL_AUTO(ewma_20_r, "%f");
    PRINT_CELL_AUTO(ewma_100_r, "%f");
    PRINT_CELL_AUTO(ewma_2_w, "%f");
    PRINT_CELL_AUTO(ewma_5_w, "%f");
    PRINT_CELL_AUTO(ewma_20_w, "%f");
    PRINT_CELL_AUTO(ewma_100_w, "%f");
    PRINT_CELL_AUTO(ewma_2_malloc_size, "%f");
    PRINT_CELL_AUTO(ewma_5_malloc_size, "%f");
    PRINT_CELL_AUTO(ewma_20_malloc_size, "%f");
    PRINT_CELL_AUTO(ewma_100_malloc_size, "%f");
    PRINT_CELL_AUTO(ewma_2_malloc_calls, "%f");
    PRINT_CELL_AUTO(ewma_5_malloc_calls, "%f");
    PRINT_CELL_AUTO(ewma_20_malloc_calls, "%f");
    PRINT_CELL_AUTO(ewma_100_malloc_calls, "%f");
    PRINT_CELL_AUTO(global_count_since_top1_percent_ewma5, "%f");
    PRINT_CELL_AUTO(global_count_since_top50_percent_ewma5, "%f");
    PRINT_CELL_AUTO(rank, "%zu");
    PRINT_CELL_AUTO(count_total, "%zu");
    PRINT_CELL_AUTO(global_count_similar, "%zu");
    PRINT_CELL_AUTO(diff, "%f");
    PRINT_CELL_AUTO(cpu_usage, "%f");
    PRINT_CELL_AUTO(disk_read_bytes, "%lld");
    PRINT_CELL_AUTO(disk_write_bytes, "%lld");
    PRINT_CELL_AUTO(syscr, "%lld");
    PRINT_CELL_AUTO(syscw, "%lld");
    PRINT_CELL_AUTO(age, "%d");

    for (int offset = -3; offset <= 3; offset++){
        char group_header[32];
        snprintf(group_header, sizeof(group_header), "group_%d_mean", offset);
        PRINT_CELL(f, row->groups[offset + 3], group_header, header, "%f");
    }

    PRINT_CELL_AUTO(model_selection, "%zu");
    PRINT_CELL_AUTO(model_score, "%f");
    PRINT_CELL_AUTO(arms_score, "%f");
    PRINT_CELL_AUTO(in_dram, "%d");

    PRINT_CELL_AUTO(read_syscalls, "%u");
    PRINT_CELL_AUTO(write_syscalls, "%u");
    PRINT_CELL_AUTO(read_bytes, "%u");
    PRINT_CELL_AUTO(write_bytes, "%u");
    PRINT_CELL_AUTO(sum_malloc_bytes, "%u");
    PRINT_CELL_AUTO(min_malloc_bytes, "%d");
    PRINT_CELL_AUTO(max_malloc_bytes, "%d");
    PRINT_CELL_AUTO(malloc_call, "%u");
    fprintf(f, "\n");
}

void print_migration_row(FILE *f, struct migration_event *event, size_t count_total, bool header) {
    PRINT_CELL(f, event->timestep, "timestep", header, "%zu");
    PRINT_CELL(f, event->va_dram, "va_dram", header, "%zu");
    PRINT_CELL(f, event->va_nvm, "va_nvm", header, "%zu");
    PRINT_CELL(f, event->read_dram, "read_dram", header, "%zu");
    PRINT_CELL(f, event->write_dram, "write_dram", header, "%zu");
    PRINT_CELL(f, event->read_nvm, "read_nvm", header, "%zu");
    PRINT_CELL(f, event->write_nvm, "write_nvm", header, "%zu");
    PRINT_CELL(f, event->time, "time_us", header, "%f");
    PRINT_CELL(f, event->type, "migration_type", header, "%d");
    PRINT_CELL(f, count_total, "count_total", header, "%zu");
    fprintf(f, "\n");
}

void pebs_write_log(){
    if (PRINT_TRAINING_DATA){
        size_t *step_to_count = malloc(sizeof(size_t) * (scores_log[logged_samples - 1].step + 1));

        FILE *f = fopen("output.txt", "w");
        if (!f) {
            perror("fopen");
            return;
        }
        print_row(f, scores_log, true); // print header
        for (size_t i = 0; i < logged_samples; i++){
            print_row(f, &scores_log[i], false);
            step_to_count[scores_log[i].step] = scores_log[i].count_total;
        }
        fclose(f);

        FILE *f_mig = fopen("migration_log.txt", "w");
        if (!f_mig) {
            perror("fopen");
            return;
        }

        print_migration_row(f_mig, migration_event_queue, 0, true);
        for (size_t i = 0; i < migration_queue_size; i++){
            print_migration_row(f_mig, &migration_event_queue[i], step_to_count[migration_event_queue[i].timestep], false);
        }
        fclose(f_mig);
    }
}

void log_row(size_t step, struct hemem_page *page, struct group_tracker *grp_tracker, size_t count_all_pages) {
    if (!PRINT_TRAINING_DATA) {
        return;
    }
    if (logged_samples >= MAX_LOGGED_SAMPLES) {
        return;
    }

    double cpu_usage = calc_cpu_usage_pct();

    scores_log[logged_samples].step = step;
    scores_log[logged_samples].page = page->va;
    scores_log[logged_samples].read = page->reads;
    scores_log[logged_samples].write = page->writes;
    scores_log[logged_samples].count = page->count;
    scores_log[logged_samples].prot = page->prot;
    scores_log[logged_samples].flags = page->flags;
    scores_log[logged_samples].ewma_2 = page->w[0];
    scores_log[logged_samples].ewma_2_r = page->w_r[0];
    scores_log[logged_samples].ewma_2_w = page->w_w[0];
    scores_log[logged_samples].ewma_2_malloc_size = page->malloc_size_ewma[0];
    scores_log[logged_samples].ewma_2_malloc_calls = page->malloc_call_ewma[0];
    scores_log[logged_samples].ewma_5 = page->w[1];
    scores_log[logged_samples].ewma_5_r = page->w_r[1];
    scores_log[logged_samples].ewma_5_w = page->w_w[1];
    scores_log[logged_samples].ewma_5_malloc_size = page->malloc_size_ewma[1];
    scores_log[logged_samples].ewma_5_malloc_calls = page->malloc_call_ewma[1];
    scores_log[logged_samples].ewma_20 = page->w[2];
    scores_log[logged_samples].ewma_20_r = page->w_r[2];
    scores_log[logged_samples].ewma_20_w = page->w_w[2];
    scores_log[logged_samples].ewma_20_malloc_size = page->malloc_size_ewma[2];
    scores_log[logged_samples].ewma_20_malloc_calls = page->malloc_call_ewma[2];
    scores_log[logged_samples].ewma_100 = page->w[3];
    scores_log[logged_samples].ewma_100_r = page->w_r[3];
    scores_log[logged_samples].ewma_100_w = page->w_w[3];
    scores_log[logged_samples].ewma_100_malloc_size = page->malloc_size_ewma[3];
    scores_log[logged_samples].ewma_100_malloc_calls = page->malloc_call_ewma[3];
    scores_log[logged_samples].global_count_since_top1_percent_ewma5 = page->global_count_since_top1_percent_ewma5;
    scores_log[logged_samples].global_count_since_top50_percent_ewma5 = page->global_count_since_top50_percent_ewma5;
    scores_log[logged_samples].rank = page->rank;
    scores_log[logged_samples].count_total = count_all_pages;
    scores_log[logged_samples].global_count_similar = page->global_count_similar;
    scores_log[logged_samples].diff = page->diff;
    scores_log[logged_samples].cpu_usage = cpu_usage;
    scores_log[logged_samples].disk_read_bytes = (curr_disk_stat.read_bytes - prev_disk_stat.read_bytes);
    scores_log[logged_samples].disk_write_bytes = (curr_disk_stat.write_bytes - prev_disk_stat.write_bytes);
    scores_log[logged_samples].syscr = (curr_disk_stat.syscr - prev_disk_stat.syscr);
    scores_log[logged_samples].syscw = (curr_disk_stat.syscw - prev_disk_stat.syscw);
    for (int i = 0; i < 7; ++i) {
        struct page_group *pg = try_get_group(grp_tracker, page->va, i);
        scores_log[logged_samples].groups[i] = pg != NULL ? pg->avg : 0.0;
    }
    scores_log[logged_samples].model_selection = page->model_selection;
    scores_log[logged_samples].model_score = page->model_score;
    scores_log[logged_samples].arms_score = page->arms_score;
    scores_log[logged_samples].in_dram = page->in_dram;
    scores_log[logged_samples].read_syscalls = page->read_syscalls;
    scores_log[logged_samples].write_syscalls = page->write_syscalls;
    scores_log[logged_samples].read_bytes = page->read_bytes;
    scores_log[logged_samples].write_bytes = page->write_bytes;
    scores_log[logged_samples].sum_malloc_bytes = page->sum_malloc_bytes;
    scores_log[logged_samples].min_malloc_bytes = page->min_malloc_bytes;
    scores_log[logged_samples].max_malloc_bytes = page->max_malloc_bytes;
    scores_log[logged_samples].malloc_call = page->malloc_call;
    scores_log[logged_samples].age = page->age;

    logged_samples++;
}