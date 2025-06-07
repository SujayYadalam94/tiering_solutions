#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <asm/unistd.h>
#include <assert.h>
#include <cmath>
#include <fcntl.h>
#include <float.h>
#include <fstream>
#include <inttypes.h>
#include <iostream>
#include <linux/hw_breakpoint.h>
#include <linux/perf_event.h>
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <unistd.h>

#include "model.hpp"

#include "groups.h"
#include "hemem.h"
#include "pebs.h"
#include "spsc-ring.h"
#include "timer.h"

#include <boost/container/deque.hpp>
#include <boost/unordered_map.hpp>

// Hash table for Hemem-handled pages
boost::unordered_map<uint64_t, struct hemem_page *> pages_map;
boost::unordered_map<uint64_t, struct hemem_page *> pages;
struct score_entry *scores;
pthread_mutex_t pages_lock = PTHREAD_MUTEX_INITIALIZER;

group_tracker *grp_tracker = NULL;
yggdrasil_decision_forests::exported_model::ModelFeatures
page_to_features(const hemem_page *page, const float &total_count) {
    std::array<struct page_group *, 4> above_groups;
    for (int i = 0; i < 4; i++) {
        above_groups[i] = grp_tracker->try_get_group(page->va, i + 1);
    }
    struct page_group *at_group = grp_tracker->try_get_group(page->va, 0);
    std::array<struct page_group *, 4> below_groups;
    for (int i = 0; i < 4; i++) {
        below_groups[i] = grp_tracker->try_get_group(page->va, -(i + 1));
    }

    return yggdrasil_decision_forests::exported_model::ModelFeatures(
        total_count, page->w[0], page->w[1], page->w[2], page->w[3],
        page->count_above_mean, page->count_below_mean, above_groups, at_group,
        below_groups);
}

static struct fifo_list dram_free_list;
static struct fifo_list nvm_free_list;

static struct migration_req_list migration_queue;
sem_t submission_sem;
sem_t completion_sem;

// Pages to be freed/added in the next interval
typedef struct mod_page {
    struct hemem_page *page;
    bool free;
} mod_page_t;
static_assert(sizeof(mod_page_t) == 16);

boost::container::deque<mod_page_t> mod_page_dq;
static pthread_mutex_t mod_page_dq_lock = PTHREAD_MUTEX_INITIALIZER;

// TODO: remove thsee as well. We shouldn't need these actually leave it; we
// need it for guardrails
static const float hist_bias[WINDOW_SIZE] = HIST_BIAS;
static const float recn_bias[WINDOW_SIZE] = RECN_BIAS;

uint32_t policy_thread_period = PEBS_KSWAPD_INTERVAL_BIG;

volatile uint64_t global_version = 0;
volatile uint8_t curr_access_version = 0;
volatile uint8_t prev_access_version; // = 1 - curr_access_version

volatile uint8_t curr_window_index = 0;
volatile uint8_t prev_window_version;

double nvm_bw_ewma = 0.0;
double nvm_bw_std = 0.0;
float promotion_cost_avg = MIN_PROMOTION_COST;
float demotion_cost_avg = MIN_DEMOTION_COST;

float min_score, max_score;

// uint64_t global_clock = 0;

uint64_t hemem_pages_cnt = 0;
uint64_t other_pages_cnt = 0;
uint64_t total_pages_cnt = 0;
uint64_t zero_pages_cnt = 0;
uint64_t throttle_cnt = 0;
uint64_t unthrottle_cnt = 0;
uint64_t cools = 0;

const float *bias = hist_bias; // History bias by default until triggered by PAR
sampling_modes sampling_mode = HIGH_FIDELITY;

static struct perf_event_mmap_page *perf_page[PEBS_NPROCS][NPBUFTYPES];
int pfd[PEBS_NPROCS][NPBUFTYPES];

volatile bool need_cool_dram = false;
volatile bool need_cool_nvm = false;

static long perf_event_open(struct perf_event_attr *hw_event, pid_t pid,
                            int cpu, int group_fd, unsigned long flags) {
    int ret;

    ret = syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
    return ret;
}
#if defined C220G5

int bw_fds[2][6];
uint64_t prev_bw_val[2][6] = {0};

uint64_t measure_nvm_bw() {
    uint64_t cur_nvm_bw = 0;
    uint64_t cur_val = 0;

    for (int j = 0; j < 2; j++) {
        for (int k = 0; k < 6; k++) {
            if (read(bw_fds[j][k], &cur_val, sizeof(cur_val)) == -1) {
                perror("Error reading bandwidth counter");
                exit(1);
            }
            cur_nvm_bw += cur_val - prev_bw_val[j][k];
            prev_bw_val[j][k] = cur_val;
        }
    }

    return cur_nvm_bw;
}
void open_perf_events(int rdwr) {
    int fd;
    struct perf_event_attr pe;

    for (unsigned long i = 0; i < 6; i++) {
        memset(&pe, 0, sizeof(pe));
        pe.type = i + 12; // TODO: read type from /sys/devices/uncore_imc_x/type
        pe.size = sizeof(pe);
        pe.disabled = 1;
        pe.inherit = 1;
        pe.config = (rdwr == 0) ? 0x304 : 0xC04;

        fd = perf_event_open(&pe, -1, 10, -1, 0);
        if (fd == -1) {
            fprintf(stderr, "Failed to open perf event for BW monitoring\n");
            exit(1);
        }
        bw_fds[rdwr][i] = fd;
    }
}

static int setup_imc_bw_counters() {
    open_perf_events(0);
    open_perf_events(1);

    // Reset the counters
    for (int j = 0; j < 2; j++) {
        for (int k = 0; k < 6; k++) {
            ioctl(bw_fds[j][k], PERF_EVENT_IOC_RESET, 0);
            ioctl(bw_fds[j][k], PERF_EVENT_IOC_ENABLE, 0);
        }
    }

    measure_nvm_bw();

    return 0;
}
#endif

static struct perf_event_mmap_page *perf_setup(__u64 config, __u64 config1,
                                               __u64 cpu, __u64 type) {
    struct perf_event_attr attr;

    memset(&attr, 0, sizeof(struct perf_event_attr));

    attr.type = PERF_TYPE_RAW;
    attr.size = sizeof(struct perf_event_attr);

    attr.config = config;
    attr.config1 = config1;
    attr.sample_period = DEFAULT_SAMPLE_PERIOD;

    attr.sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_ADDR;
    attr.pinned = 1;
    attr.disabled = 0;
    attr.exclude_kernel = 1;
    attr.exclude_hv = 1;
    attr.exclude_callchain_kernel = 1;
    attr.exclude_callchain_user = 1;
    attr.precise_ip = 1;

    pfd[cpu][type] = perf_event_open(&attr, -1, cpu, -1, 0);
    if (pfd[cpu][type] == -1) {
        perror("perf_event_open");
    }
    assert(pfd[cpu][type] != -1);

    size_t mmap_size = sysconf(_SC_PAGESIZE) * PERF_PAGES;
    struct perf_event_mmap_page *p = (struct perf_event_mmap_page *)mmap(
        NULL, mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, pfd[cpu][type], 0);
    if (p == MAP_FAILED) {
        perror("mmap");
    }
    assert(p != MAP_FAILED);

    return p;
}

static void update_sampling_frequency() {
    int ret = 0;
    uint64_t sample_period = DEFAULT_SAMPLE_PERIOD;

    if (sampling_mode == HIGH_FIDELITY) {
        sample_period = HF_SAMPLE_PERIOD;
    }

    for (int i = 0; i < PEBS_NPROCS; i++) {
#if defined C220G5
        if (i >= 10 && i < 20) {
            continue;
        }
#endif
        for (int j = 0; j < NPBUFTYPES; j++) {
            ret = ioctl(pfd[i][j], PERF_EVENT_IOC_PERIOD, &sample_period);
            if (ret != 0) {
                perror("PERF_EVENT_IOC_PERIOD");
            }
        }
    }
}

void *pebs_scan_thread(void *_) {

    cpu_set_t cpuset;
    pthread_t thread;

    thread = pthread_self();
    CPU_ZERO(&cpuset);
    CPU_SET(SCANNING_THREAD_CPU, &cpuset);
    int s = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
    if (s != 0) {
        perror("pthread_setaffinity_np");
        assert(0);
    }

    for (;;) {
        for (int i = 0; i < PEBS_NPROCS; i++) {
#if defined C220G5
            if (i >= 10 && i < 20) {
                continue;
            }
#endif
            for (int j = 0; j < NPBUFTYPES; j++) {
                struct perf_event_mmap_page *p = perf_page[i][j];
                char *pbuf = (char *)p + p->data_offset;

                __sync_synchronize();

                if (p->data_head == p->data_tail) {
                    continue;
                }

                struct perf_event_header *ph =
                    (struct perf_event_header *)(pbuf +
                                                 (p->data_tail % p->data_size));
                struct perf_sample *ps;
                struct hemem_page *page;

                switch (ph->type) {
                case PERF_RECORD_SAMPLE:
                    ps = (struct perf_sample *)ph;
                    assert(ps != NULL);
                    if (ps->addr != 0) {
                        __u64 pfn = ps->addr & HUGE_PFN_MASK;
                        page = pebs_find_page(pfn);
                        if (page != NULL) {
                            if (page->va != 0) {
                                page->accesses[j][curr_access_version]++;
                            }
                            hemem_pages_cnt++;
                        } else {
                            other_pages_cnt++;
                        }
                        total_pages_cnt++;
                    } else {
                        zero_pages_cnt++;
                    }
                    break;
                case PERF_RECORD_THROTTLE:
                case PERF_RECORD_UNTHROTTLE:
                    if (ph->type == PERF_RECORD_THROTTLE) {
                        throttle_cnt++;
                    } else {
                        unthrottle_cnt++;
                    }
                    break;
                default:
                    break;
                }

                p->data_tail += ph->size;
            }
        }
    }

    return NULL;
}

static void pebs_migrate_down(struct hemem_page *page, uint64_t offset) {
    struct timeval start, end;

    gettimeofday(&start, NULL);

    page->migrating = true;
    hemem_wp_page(page, true);
    hemem_migrate_down(page, offset);
    page->migrating = false;

    gettimeofday(&end, NULL);
    MY_LOG_TIME("migrate_down: %f s\n", elapsed(&start, &end));
}

static void pebs_migrate_up(struct hemem_page *page, uint64_t offset) {
    struct timeval start, end;

    gettimeofday(&start, NULL);

    page->migrating = true;
    hemem_wp_page(page, true);
    hemem_migrate_up(page, offset);
    page->migrating = false;

    gettimeofday(&end, NULL);
    MY_LOG_TIME("migrate_up: %f s\n", elapsed(&start, &end));
}

// Sorts in ascending order
inline int ulong_sort_cmp(const void *a, const void *b) {
    const uint64_t &_a = *(const uint64_t *)a;
    const uint64_t &_b = *(const uint64_t *)b;
    return (_a < _b) ? -1 : (_a > _b);
}

// Sorts in descending order
inline int sort_entry_cmp(const void *a, const void *b) {
    const struct score_entry &_a = *(const struct score_entry *)a;
    const struct score_entry &_b = *(const struct score_entry *)b;

    return (_a.score > _b.score) ? -1 : (_a.score < _b.score);
}

static inline float window_compress(const float (&v)[3]) {
    return (v[0] + v[1] + v[2]) / 3.0f;
}

static inline float model_loss(const float (&observed)[3],
                               const float model_pred, const float &huer_pred) {
    return abs(window_compress(observed) - model_pred) -
           abs(window_compress(observed) - huer_pred);
}

static size_t calculate_scores_map(struct score_entry *scores_out,
                                   const float *bias) {
    struct ptimer window_timer;
    ptimer_init(&window_timer, "Scores (window)");

    struct hemem_page *page;
    size_t s_idx = 0;
    float max_ewma2 = -1;

    size_t pages_cnt = pages_map.size();
    if (pages_cnt == 0) {
        return 0; // no pages to process
    }

    // Iterate over the pages
    grp_tracker->reset_group_hash();

    static std::ofstream ofs("output.txt");
    static int timestep = 0;
    timestep++;

    float count_total = 0;
    for (auto &it : pages_map) {
        const uint64_t &key = it.first;
        struct hemem_page *page = it.second;

        update_window(page, prev_access_version, sampling_mode);

        float accesses = calculate_accesses(page, prev_access_version);

        ofs << timestep << "," << page->va << "," << accesses << ","
            << page->model_selection << ","
            << page->model_score[page->score_index] << ","
            << page->arms_score[page->score_index] << "," << page->in_dram
            << std::endl;

        // TODO update the group entry
        grp_tracker->update_group_entry(page->va, accesses);
        page->access_history[page->score_index] = accesses;
        constexpr uint8_t MAX_SCORE_ENTRIES =
            (sizeof(page->model_score) / sizeof(page->model_score[0]));
        page->score_index = (page->score_index + 1) % MAX_SCORE_ENTRIES;

        count_total += accesses;

        // Reset the access counts
        page->accesses[DRAMREAD][prev_access_version] = 0;
        page->accesses[NVMREAD][prev_access_version] = 0;
        page->accesses[WRITE][prev_access_version] = 0;
    }
    std::cout << "Total accesses: " << count_total << std::endl;

    // build the features
    std::vector<yggdrasil_decision_forests::exported_model::ModelFeatures>
        features;

    float avg_loss = 0;
    int count = 0;

    for (auto &it : pages_map) {
        const uint64_t &key = it.first;
        struct hemem_page *page = it.second;

        features.push_back(page_to_features(page, count_total)); // max_ewma2));

        // Update the Model Accuracy
        page->accuracy = ewma(page->accuracy,
                              model_loss(page->access_history,
                                         page->model_score[page->score_index],
                                         page->arms_score[page->score_index]),
                              2.0 / 21.0); // window size of 20

        avg_loss += page->accuracy;
        count++;

        if (page->accuracy > 1000.0f) {
            page->model_selection = prediction_type::ARMS;
        } else if (page->accuracy < -1000.0f) {
            page->model_selection = prediction_type::MODEL;
        }

        // Calculate the hotness score
        // page->prev_score = page->score;
        // this is the ARMS score
        // page->score = compute_score(page, bias);
        // scores_out[s_idx++] =
        //    (struct score_entry){page, page->score * max_ewma2};

        // scores_out[s_idx++] = (struct score_entry){page, page->score};
    }

    std::cout << "Avg loss: " << (avg_loss / count) << std::endl;

    // Load the model (to do only once).
    namespace ydf = yggdrasil_decision_forests;
    auto model = ydf::exported_model::Load("sqlite_4GB_ycsba");

    // Run the model
    size_t prediction_index = 0;
    auto predictions = (*model)->Predict(features, count_total);

    for (auto &it : pages_map) {
        const uint64_t &key = it.first;
        struct hemem_page *page = it.second;

        // Calculate the hotness score
        page->prev_score = page->score;

        page->model_score[page->score_index] = predictions[prediction_index++];
        page->arms_score[page->score_index] = compute_score(page, bias);
        if (page->model_selection == prediction_type::MODEL) {
            page->score = page->model_score[page->score_index];
        } else if (page->model_selection == prediction_type::ARMS) {
            page->score = page->arms_score[page->score_index];
        } else {
            assert(0);
        }

        scores_out[s_idx++] = (struct score_entry){page, page->score};
    }

    ptimer_print(&window_timer);

    return s_idx;
}

static inline int continue_migration(struct hemem_page *hp,
                                     struct hemem_page *cp) {
    // Compare the min of hot page and max of cold page
    // A hot page should hav all EWMAs greater than the max EWMA of a cold page
    float hot_page_min_avg = hp->w[0];
    float cold_page_max_avg = cp->w[WINDOW_SIZE - 1];

    for (int i = 1; i < WINDOW_SIZE; i++) {
        if (hp->w[i] < hot_page_min_avg) {
            hot_page_min_avg = hp->w[i];
        }
        if (cp->w[i] > cold_page_max_avg) {
            cold_page_max_avg = cp->w[i];
        }
    }

    if (hot_page_min_avg < cold_page_max_avg) {
        return 0;
    }

    // Cost-benefit analysis
    float cost = 1.5 * (promotion_cost_avg + demotion_cost_avg);
    float latency_diff = 0.1;

    if (demotion_cost_avg > 20000) {
        latency_diff = demotion_cost_avg /
                       (PAGE_SIZE / 64); // Number of cachelines in a page
        latency_diff -= 0.1;
    }
    float benefit =
        (hp->score - cp->score) * hp->hot_age * HF_SAMPLE_PERIOD * latency_diff;

    if (benefit < cost) {
        return 0;
    }

    return 1;
}

void promote_to_free_dram_page(struct hemem_page *p, struct hemem_page *np) {
    uint64_t old_offset;

    // There could be a possible race with pebs_remove_page()
    // So acquire lock to ensure page is not removed while being migrated
    pthread_mutex_lock(&(p->page_lock));
    if (!p->present) {
        // Don't migrate as this page is being removed
        // Put np back on the dram_free_list because we are not going to migrate
        enqueue_fifo(&dram_free_list, np);
        pthread_mutex_unlock(&(p->page_lock));
        return;
    }

    old_offset = p->devdax_offset;
    pebs_migrate_up(p, np->devdax_offset);
    // We can release the lock now that migration is complete
    pthread_mutex_unlock(&(p->page_lock));

    // Reset the page fields
    np->devdax_offset = old_offset;
    np->in_dram = false;
    np->present = false;
    reset_page_access_fields(np);

    enqueue_fifo(&nvm_free_list, np);
}

bool demote_to_free_nvm_page(struct hemem_page *cp, struct hemem_page *np) {
    uint64_t old_offset;

    // There could be a possible race with pebs_remove_page()
    // So acquire lock to ensure page is not removed while being migrated
    pthread_mutex_lock(&(cp->page_lock));
    if (!cp->present) {
        // Don't migrate as this page is being removed
        pthread_mutex_unlock(&(cp->page_lock));
        return false;
    }

    old_offset = cp->devdax_offset;
    pebs_migrate_down(cp, np->devdax_offset);

    pthread_mutex_unlock(&(cp->page_lock));

    // Reset the page fields
    np->devdax_offset = old_offset;
    np->in_dram = true;
    np->present = false;
    reset_page_access_fields(np);

    // Don't add the page to the free list because
    // it will be used immediately after
    // enqueue_fifo(&dram_free_list, np);
    return true;
}

void *pebs_migration_thread(void *_) {
    struct migration_req *req;
    struct ptimer migrate_timer;

    ptimer_init(&migrate_timer, "Migrate");

    while (true) {
        sem_wait(&submission_sem);

        while (true) {
            req = dequeue_fifo_m(&migration_queue);
            if (req == NULL) {
                break;
            }

            // Demote a page if necessary
            if (req->need_demotion) {
                ptimer_start(&migrate_timer);
                if (!demote_to_free_nvm_page(req->dram_page, req->free_page)) {
                    enqueue_fifo(&nvm_free_list, req->free_page);
                    sem_post(&completion_sem);
                    free(req);
                    continue;
                }
                ptimer_stop(&migrate_timer);
                demotion_cost_avg =
                    (MIGRATION_COST_ALPHA * migrate_timer.elapsed_us) +
                    ((1 - MIGRATION_COST_ALPHA) * demotion_cost_avg);
            }

            // Promote the hot NVM page
            ptimer_start(&migrate_timer);
            promote_to_free_dram_page(req->nvm_page, req->free_page);
            ptimer_stop(&migrate_timer);
            promotion_cost_avg =
                (MIGRATION_COST_ALPHA * migrate_timer.elapsed_us) +
                ((1 - MIGRATION_COST_ALPHA) * promotion_cost_avg);

            sem_post(&completion_sem);
            free(req);
        }
    }
}

void *pebs_policy_thread(void *_) {
    struct ptimer loop_timer, tree_timer, score_timer, sort_timer, id_timer;
    struct ptimer remaining_timer;
    ptimer_init(&loop_timer, "Loop");
    ptimer_init(&tree_timer, "Tree");
    ptimer_init(&score_timer, "Score");
    ptimer_init(&sort_timer, "Sort");
    ptimer_init(&id_timer, "Identify");
    ptimer_init(&remaining_timer, "Remaining");

    cpu_set_t cpuset;
    pthread_t thread;
    // int tries;
    struct hemem_page *p;
    struct hemem_page *cp;
    struct hemem_page *np;
    uint64_t migrated_bytes;
    // uint64_t old_offset;
    double migrate_time_us;
    struct hemem_page *page = NULL;

    size_t s_pages_cnt;

    struct migration_req *m_req;

    uint64_t promote_idx = 0;
    uint64_t demote_idx = 0;
    size_t migrated_pages = 0;
    size_t num_migration_jobs = 0;
    float batch_size = NUM_MIGRATION_THREADS;

    float cur_nvm_bw = 0;
    float cusum = 0;
    uint32_t time_since_recn = 0;

    uint32_t max_migrations_cur_interval =
        (policy_thread_period) / (promotion_cost_avg + demotion_cost_avg);

    // Use a dedicated CPU core for the policy thread
    thread = pthread_self();
    CPU_ZERO(&cpuset);
    CPU_SET(MIGRATION_THREAD_CPU, &cpuset);
    int s = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
    if (s != 0) {
        perror("pthread_setaffinity_np");
        assert(0);
    }

    // Initialize memory controller BW counters
    setup_imc_bw_counters();

    // Sleep first to allow the scanning thread to start
    usleep((uint64_t)((1.0 * policy_thread_period)));

    for (;;) {
        ptimer_start(&loop_timer);
        ptimer_start(&remaining_timer);

        fprintf(MY_LOG_STREAM, "\n========================================\n");
        fprintf(MY_LOG_STREAM, "Starting new interval\n");
        fprintf(MY_LOG_STREAM, "========================================\n");

        // Update the window index (circular buffer)
        curr_window_index = global_version % WINDOW_SIZE;
        // "Bump" the global version to indicate that we are starting a new
        // interval
        global_version++;
        prev_access_version = curr_access_version;
        curr_access_version = 1 - curr_access_version;
        __sync_synchronize();

        // Compute peak-to-average ratio every 1 second
        if (global_version % (1000000 / policy_thread_period) == 0) {
            cur_nvm_bw = (measure_nvm_bw() * 64.0) / (1024 * 1024 * 1024);

            // TODO: Remove this entire block of stuff switching between bias.
            // We don't need anymore
            nvm_bw_ewma = 0.7 * nvm_bw_ewma + 0.3 * cur_nvm_bw;
            nvm_bw_std =
                (0.9 * nvm_bw_std * nvm_bw_std) +
                0.1 * (cur_nvm_bw - nvm_bw_ewma) * (cur_nvm_bw - nvm_bw_ewma);
            nvm_bw_std = sqrt(nvm_bw_std);

            // Page-Hinkley test
            cusum += ((cur_nvm_bw - nvm_bw_ewma) - 0.1);
            if (cusum > 3 * nvm_bw_std) {
                if (bias == hist_bias && cur_nvm_bw > 0.3) {
                    bias = recn_bias;
                    fprintf(MY_LOG_STREAM, "Switching to RECN bias\n");
                    time_since_recn = 0;
                }
                cusum = 0;
            } else if (time_since_recn >= 20 && cusum < 0) {
                if (bias == recn_bias) {
                    bias = hist_bias;
                    fprintf(MY_LOG_STREAM, "Switching back to HIST bias\n");
                }
            }

            if (cusum < -2) {
                cusum = 0;
            }

            if (bias == recn_bias) {
                time_since_recn++;
            }

            batch_size = ((MAX_NVM_WR_BW - cur_nvm_bw) / MAX_NVM_WR_BW) *
                         NUM_MIGRATION_THREADS;
            batch_size = floor(batch_size);
            if (batch_size < 1) {
                batch_size = 1;
            }

            fprintf(
                MY_LOG_STREAM,
                "NVM bw: %f, NVM bw EWMA: %f, NVM bw stddev: %f, cusum: %f\n",
                cur_nvm_bw, nvm_bw_ewma, nvm_bw_std, cusum);
        }

        // free pages using free page ring buffer
        ptimer_start(&tree_timer);
        while (true) {
            mod_page_t mp;
            pthread_mutex_lock(&mod_page_dq_lock);
            if (mod_page_dq.size() == 0) {
                pthread_mutex_unlock(&mod_page_dq_lock);
                break;
            }
            mp = mod_page_dq.front();
            mod_page_dq.pop_front();
            pthread_mutex_unlock(&mod_page_dq_lock);

            page = mp.page;
            // fprintf(stderr, "Processing page %lu [va: %lu]\n", page,
            // page->va);

            if (mp.free) {
                if (pages_map.size() != 0 &&
                    pages_map.find(page->va) != pages_map.end()) {
                    pages_map.erase(page->va);

                    // Add page to correct free list
                    if (page->in_dram) {
                        enqueue_fifo(&dram_free_list, page);
                    } else {
                        enqueue_fifo(&nvm_free_list, page);
                    }
                    reset_page_access_fields(page);
                } else {
                    fprintf(MY_LOG_STREAM, "WARNING: Page not found in map\n");
                }
            } else {
                pages_map[page->va] = page;
                grp_tracker->add_group_if_missing(page->va);
            }
        }
        ptimer_stop_and_print(&tree_timer);

        // Calculate the scores
        ptimer_start(&score_timer);

        s_pages_cnt = calculate_scores_map(scores, bias);

        ptimer_stop_and_print(&score_timer);

        // Sort the scores (in descending order)
        ptimer_start(&sort_timer);
        qsort(scores, s_pages_cnt, sizeof(struct score_entry), sort_entry_cmp);
        ptimer_stop_and_print(&sort_timer);

        // Set the top_since_iter for the top pages
        for (uint_fast64_t k = 0; k < dramsize / PAGE_SIZE && k < s_pages_cnt;
             k++) {
            struct hemem_page *top_page = scores[k].page;
            if (scores[k].score != 0) {
                top_page->hot_age++;

                if (top_page->hot_age > 1 &&
                    (top_page->score >= top_page->prev_score)) {
                    // Page has continued to stay hot, so can be promoted
                    top_page->can_promote = true;
                } else if (top_page->model_selection ==
                           prediction_type::MODEL) {
                    // I don't care if the page is continuously hot if it is
                    // from the model
                    top_page->can_promote = true;
                }
            }
        }
        for (uint_fast64_t k = dramsize / PAGE_SIZE; k < s_pages_cnt; k++) {
            scores[k].page->hot_age = 0;
            scores[k].page->can_promote = false;
        }

        if (s_pages_cnt == 0) {
            goto loop_end;
        }
        min_score = scores[s_pages_cnt - 1].score;
        max_score = scores[0].score;

        fprintf(MY_LOG_STREAM,
                "min_score: %.3f (%.3f %.3f), max_score: %.3f (%.3f %.3f)\n",
                min_score, scores[s_pages_cnt - 1].page->w[0],
                scores[s_pages_cnt - 1].page->w[1], max_score,
                scores[0].page->w[0], scores[0].page->w[1]);
        fprintf(MY_LOG_STREAM, "Prom cost: %f, Dem cost: %f\n",
                promotion_cost_avg, demotion_cost_avg);

        // Perform migrations
        ptimer_reset(&id_timer);

        promote_idx = 0;
        demote_idx = s_pages_cnt - 1;
        migrated_pages = 0;

        // Before starting migrations of this interval, check for completion of
        // previous migrations
        while (num_migration_jobs > 0) {
            sem_wait(&completion_sem);
            num_migration_jobs--;
        }

        ptimer_stop(&remaining_timer);

        /*******************/
        /* MIGRATIONs LOOP*/
        migrated_bytes = 0;
        num_migration_jobs = 0;
        max_migrations_cur_interval =
            ((policy_thread_period) /
             (promotion_cost_avg + demotion_cost_avg)) *
            batch_size;
        while (promote_idx < dramsize / PAGE_SIZE && promote_idx < demote_idx) {
            // If we have scheduled the maximum number of migrations for this
            // interval, stop
            if (num_migration_jobs >= max_migrations_cur_interval) {
                fprintf(MY_LOG_STREAM, "Scheduled %lu migrations\n",
                        num_migration_jobs);
                break;
            }

            if (scores[promote_idx].score == 0)
                break;
            // find the hotest NVM page that needs to be promoted
            ptimer_continue(&id_timer);
            while (promote_idx < demote_idx &&
                   scores[promote_idx].page->in_dram) {
                promote_idx++;
            }
            if (promote_idx >= demote_idx) {
                break;
            }
            p = scores[promote_idx].page;
            assert(!p->in_dram);

            if (!(p->can_promote)) {
                promote_idx++;
                continue;
            }

            // try to find a free DRAM page
            np = dequeue_fifo(&dram_free_list);
            if (np != NULL) {
                assert(!(np->present));
                ptimer_stop(&id_timer);

                // Cost-benefit analysis
                float cost = 1.5 * (promotion_cost_avg + demotion_cost_avg);
                float latency_diff = 0.1;
                if (demotion_cost_avg > 20000) {
                    latency_diff =
                        demotion_cost_avg /
                        (PAGE_SIZE / 64); // Number of cachelines in a page
                    latency_diff -= 0.1;
                }
                float benefit =
                    p->score * p->hot_age * HF_SAMPLE_PERIOD * latency_diff;
                if (benefit < cost) {
                    enqueue_fifo(&dram_free_list, np);
                    break;
                }

                fprintf(MY_LOG_STREAM,
                        "Promoting freely at %lu: 0x%lx score: %f (%f %f)\n",
                        promote_idx, p->va, p->score, p->w[0], p->w[1]);

                m_req = (struct migration_req *)malloc(
                    sizeof(struct migration_req));
                memset(m_req, 0, sizeof(struct migration_req));
                m_req->nvm_page = p;
                m_req->free_page = np;
                m_req->need_demotion = false;

                enqueue_fifo_m(&migration_queue, m_req);

                num_migration_jobs++;
                migrated_bytes += pt_to_pagesize(p->pt);
                migrated_pages++;

                if (num_migration_jobs <= batch_size) {
                    // Wake up only as many threads as the batch size
                    // This is to avoid waking up all threads and then blocking
                    // them
                    sem_post(&submission_sem);
                }

                promote_idx++;
                continue;
            }

            // Find the coldest DRAM page that needs to be demoted
            while (demote_idx > promote_idx &&
                   !scores[demote_idx].page->in_dram) {
                demote_idx--;
            }
            if (demote_idx <= promote_idx ||
                demote_idx <= (dramsize / PAGE_SIZE)) {
                break;
            }

            cp = scores[demote_idx].page;
            assert(cp->in_dram && cp->va > 0);

            if (!continue_migration(p, cp)) {
                break;
            }

            // try to find a free NVM page
            np = dequeue_fifo(&nvm_free_list);
            assert(np != NULL);
            ptimer_stop(&id_timer);

            fprintf(MY_LOG_STREAM, "Demoting at %ld: 0x%lx score: %f (%f %f)\n",
                    demote_idx, cp->va, cp->score, cp->w[0], cp->w[1]);
            fprintf(MY_LOG_STREAM,
                    "Promoting at %ld: 0x%lx score: %f (%f %f)\n", promote_idx,
                    p->va, p->score, p->w[0], p->w[1]);

            // move the cold DRAM page to NVM
            m_req =
                (struct migration_req *)malloc(sizeof(struct migration_req));
            memset(m_req, 0, sizeof(struct migration_req));
            m_req->dram_page = cp;
            m_req->nvm_page = p;
            m_req->free_page = np;
            m_req->need_demotion = true;

            enqueue_fifo_m(&migration_queue, m_req);

            num_migration_jobs++;
            migrated_bytes += (2 * pt_to_pagesize(cp->pt));
            migrated_pages += 2;
            promote_idx++;
            demote_idx--;

            if (num_migration_jobs <= batch_size) {
                // Wake up only as many threads as the batch size
                // This is to avoid waking up all threads and then blocking them
                sem_post(&submission_sem);
            }
        }

    loop_end:
        ptimer_print(&id_timer);
        ptimer_stop_and_print(&loop_timer);
        ptimer_stop(&remaining_timer);

        fprintf(MY_LOG_STREAM,
                "Migrated %lu pages (%lu bytes) in this interval\n",
                migrated_pages, migrated_bytes);
        if (migrated_pages == 0) {
            // Reset the migration cost averages
            // TOOD: Think about the best way to reset migration costs
            promotion_cost_avg /= 1.5;
            demotion_cost_avg /= 1.5;
            if (promotion_cost_avg < MIN_PROMOTION_COST) {
                promotion_cost_avg = MIN_PROMOTION_COST;
            }
            if (demotion_cost_avg < MIN_DEMOTION_COST) {
                demotion_cost_avg = MIN_DEMOTION_COST;
            }
        }

        migrate_time_us = loop_timer.elapsed_us;
        if (migrate_time_us < (1.0 * policy_thread_period)) {
            usleep((uint64_t)((1.0 * policy_thread_period) - migrate_time_us));
        }
    }

    return NULL;
}

static struct hemem_page *pebs_allocate_page() {
    struct timeval start, end;
    struct hemem_page *page;

    gettimeofday(&start, NULL);
    page = dequeue_fifo(&dram_free_list);
    if (page != NULL) {
        assert(page->in_dram);
        assert(!page->present);

        page->present = true;

        gettimeofday(&end, NULL);
        MY_LOG_TIME("mem_policy_allocate_page: %f s\n", elapsed(&start, &end));

        return page;
    }

    // DRAM is full, fall back to NVM
    page = dequeue_fifo(&nvm_free_list);
    if (page != NULL) {
        assert(!page->in_dram);
        assert(!page->present);

        page->present = true;

        gettimeofday(&end, NULL);
        MY_LOG_TIME("mem_policy_allocate_page: %f s\n", elapsed(&start, &end));

        return page;
    }

    assert(!"Out of memory");
}

struct hemem_page *pebs_pagefault(void) {
    struct hemem_page *page;

    // do the heavy lifting of finding the devdax file offset to place the page
    page = pebs_allocate_page();
    assert(page != NULL);

    return page;
}

void pebs_add_page(struct hemem_page *page) {
    int absent;
    assert(page != NULL);
    MY_LOG("pebs: add page, put this page into add_pages_ring: va: 0x%lx\n",
           page->va);

    // Add to the hash table
    pthread_mutex_lock(&pages_lock);
    pages[page->va] = page;
    grp_tracker->add_group_if_missing(page->va);
    pthread_mutex_unlock(&pages_lock);

    // Add to the new pages ring
    pthread_mutex_lock(&mod_page_dq_lock);
    mod_page_t mp = (mod_page_t){.page = page, .free = false};
    mod_page_dq.push_back(mp);
    pthread_mutex_unlock(&mod_page_dq_lock);
}

struct hemem_page *pebs_find_page(uint64_t va) {
    if (pages.empty()) {
        return NULL;
    }
    pthread_mutex_lock(&pages_lock);

    auto page = pages.find(va);
    pthread_mutex_unlock(&pages_lock);
    if (page == pages.end()) {
        return NULL;
    }
    return page->second;
}

void pebs_remove_page(struct hemem_page *page) {
    assert(page != NULL);
    MY_LOG("pebs: remove page, put this page into free_page_ring: va: 0x%lx\n",
           page->va);

    // Remove page from hash table
    pthread_mutex_lock(&pages_lock);
    pages.erase(page->va);
    pthread_mutex_unlock(&pages_lock);

    pthread_mutex_lock(&mod_page_dq_lock);
    mod_page_t mp = (mod_page_t){.page = page, .free = true};
    mod_page_dq.push_back(mp);
    pthread_mutex_unlock(&mod_page_dq_lock);

    // We set page->present to false so that
    // the migration thread does not migrate this page
    pthread_mutex_lock(&(page->page_lock));
    page->present = false;
    pthread_mutex_unlock(&(page->page_lock));
}

#if defined C220G5
#define L3_LOAD_MISS_LOCAL 0x1d3
#define L3_LOAD_MISS_REMOTE 0x2d3
#endif

void pebs_init(void) {
    pthread_t kswapd_thread;
    pthread_t scan_thread;
    pthread_t migration_threads[NUM_MIGRATION_THREADS];

    MY_LOG("pebs_init: started\n");

    for (int i = 0; i < PEBS_NPROCS; i++) {
#ifdef JOSEPM
        if (i >= 8 && i < 16) {
            continue;
        }
#elif defined C220G5
        if (i >= 10 && i < 20) {
            continue;
        }
#endif
        perf_page[i][DRAMREAD] =
            perf_setup(L3_LOAD_MISS_LOCAL, 0, i,
                       DRAMREAD); // MEM_LOAD_L3_MISS_RETIRED.LOCAL_DRAM
        perf_page[i][NVMREAD] =
            perf_setup(L3_LOAD_MISS_REMOTE, 0, i,
                       NVMREAD); // MEM_LOAD_RETIRED.LOCAL_PMM
        perf_page[i][WRITE] =
            perf_setup(0x82d0, 0, i, WRITE); // MEM_INST_RETIRED.ALL_STORES
    }

    pthread_mutex_init(&(dram_free_list.list_lock), NULL);
    for (uint_fast64_t i = 0; i < dramsize / PAGE_SIZE; i++) {
        struct hemem_page *p =
            (struct hemem_page *)calloc(1, sizeof(struct hemem_page));
        p->devdax_offset = i * PAGE_SIZE;
        p->present = false;
        p->in_dram = true;
        p->pt = pagesize_to_pt(PAGE_SIZE);
        pthread_mutex_init(&(p->page_lock), NULL);

        enqueue_fifo(&dram_free_list, p);
    }

    pthread_mutex_init(&(nvm_free_list.list_lock), NULL);
    for (uint_fast64_t i = 0; i < nvmsize / PAGE_SIZE; i++) {
        struct hemem_page *p =
            (struct hemem_page *)calloc(1, sizeof(struct hemem_page));
        p->devdax_offset = i * PAGE_SIZE;
        p->present = false;
        p->in_dram = false;
        p->pt = pagesize_to_pt(PAGE_SIZE);
        pthread_mutex_init(&(p->page_lock), NULL);

        enqueue_fifo(&nvm_free_list, p);
    }

    grp_tracker = new group_tracker();

    scores = (struct score_entry *)malloc((MAX_NVME_PAGES + MAX_DRAM_PAGES) *
                                          sizeof(struct score_entry));

    // Initialize bias values
    for (int i = 0; i < WINDOW_SIZE; i++) {
        printf("w_ewma_alpha[%d] = %f\n", i, w_ewma_alpha[i]);
    }
    for (int i = 0; i < WINDOW_SIZE; i++) {
        printf("hist_bias[%d] = %f\n", i, hist_bias[i]);
    }
    for (int i = 0; i < WINDOW_SIZE; i++) {
        printf("recn_bias[%d] = %f\n", i, recn_bias[i]);
    }

    // Start the policy and scan threads
    int r = pthread_create(&scan_thread, NULL, pebs_scan_thread, NULL);
    assert(r == 0);

    r = pthread_create(&kswapd_thread, NULL, pebs_policy_thread, NULL);
    assert(r == 0);

    for (int i = 0; i < NUM_MIGRATION_THREADS; i++) {
        r = pthread_create(&migration_threads[i], NULL, pebs_migration_thread,
                           NULL);
        assert(r == 0);
    }
    sem_init(&submission_sem, 0, 0);
    sem_init(&completion_sem, 0, 0);
    pthread_mutex_init(&migration_queue.list_lock, NULL);

    if ((dramsize + nvmsize) < (32 * 1024 * 1024 * 1024UL)) {
        policy_thread_period = PEBS_KSWAPD_INTERVAL_SMALL;
    } else {
        policy_thread_period = PEBS_KSWAPD_INTERVAL_BIG;
    }

    MY_LOG("Memory management policy is PEBS\n");

    MY_LOG("pebs_init: finished\n");
}

void pebs_shutdown() {
    for (int i = 0; i < PEBS_NPROCS; i++) {
        for (int j = 0; j < NPBUFTYPES; j++) {
            ioctl(pfd[i][j], PERF_EVENT_IOC_DISABLE, 0);
        }
    }
}

void pebs_stats() {
    MY_LOG_STATS(
        "samples:[%ld/%ld] throttle/unthrottle_cnt:[%ld/%ld] cools:[%ld]\n",
        hemem_pages_cnt, total_pages_cnt, throttle_cnt, unthrottle_cnt, cools);
}
