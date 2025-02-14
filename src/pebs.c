#define _GNU_SOURCE
#include <stdlib.h>
#include <pthread.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>
#include <pthread.h>
#include <assert.h>
#include <sys/time.h>
#include <unistd.h>
#include <asm/unistd.h>
#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>
#include <sys/mman.h>
#include <sched.h>
#include <sys/ioctl.h>

#include "hemem.h"
#include "pebs.h"
#include "timer.h"
#include "spsc-ring.h"

#include "kdq.h"
#include "kbtree.h"
#include "uthash.h"

// Hash table for Hemem-handled pages
struct hemem_page *pages = NULL;
pthread_mutex_t pages_lock = PTHREAD_MUTEX_INITIALIZER;

//#define ktree_cmp(a,b) ((a) < (b.va) ? -1 : (a.va) > (b.va))
//#define ktree_cmp(a,b) (                                                      \
  ((((struct hemem_page*)a)->va) < (((struct hemem_page*)b)->va))             \
    ? -1 : ((((struct hemem_page*)a)->va) > (((struct hemem_page*)b)->va)) )
//KBTREE_INIT(kPagesTree, struct hemem_page*, ktree_cmp)

typedef struct {
  struct hemem_page* page;
  uint64_t va;
} page_tree_entry_t;

#define ktree_cmp(a,b) (((a).va) < ((b).va) ? -1 : ((a).va) > ((b).va))
KBTREE_INIT(kPagesTree, page_tree_entry_t, ktree_cmp);

kbtree_t(kPagesTree) *pages_tree;

struct score_entry *scores;

//static struct fifo_list dram_hot_list;
//static struct fifo_list dram_cold_list;
//static struct fifo_list nvm_hot_list;
//static struct fifo_list nvm_cold_list;

static struct fifo_list dram_free_list;
static struct fifo_list nvm_free_list;

//static ring_handle_t promote_page_ring;
//static ring_handle_t demote_page_ring;

// Pages to be freed/added in the next interval
typedef struct mod_page {
  struct hemem_page* page;
  bool free;
} mod_page_t;
static_assert(sizeof(mod_page_t) == 16);

KDQ_INIT(mod_page_t);

static kdq_t(mod_page_t) *mod_page_dq;
static pthread_mutex_t mod_page_dq_lock = PTHREAD_MUTEX_INITIALIZER;

/*
static ring_handle_t free_page_ring;
static pthread_mutex_t free_page_ring_lock = PTHREAD_MUTEX_INITIALIZER;
static ring_handle_t add_pages_ring;
static pthread_mutex_t add_pages_ring_lock = PTHREAD_MUTEX_INITIALIZER;
*/

static float w_ewma_alpha[WINDOW_SIZE];
static uint8_t hist_bias[WINDOW_SIZE];
static uint8_t recn_bias[WINDOW_SIZE];

// Neighbour buffers
static ring_handle_t l_neighbours;
static ring_handle_t r_neighbours;

volatile uint64_t global_version = 0;
volatile uint8_t curr_access_version = 0;
volatile uint8_t prev_access_version; // = 1 - curr_access_version

volatile uint8_t curr_window_index = 0;
volatile uint8_t prev_window_version;

volatile uint32_t nvm_page_accesses_curr = 0;
uint32_t nvm_page_accesses_prev;
double nvm_page_ewma = 0.0;

//uint64_t global_clock = 0;

uint64_t hemem_pages_cnt = 0;
uint64_t other_pages_cnt = 0;
uint64_t total_pages_cnt = 0;
uint64_t zero_pages_cnt = 0;
uint64_t throttle_cnt = 0;
uint64_t unthrottle_cnt = 0;
uint64_t cools = 0;

static struct perf_event_mmap_page *perf_page[PEBS_NPROCS][NPBUFTYPES];
int pfd[PEBS_NPROCS][NPBUFTYPES];

volatile bool need_cool_dram = false;
volatile bool need_cool_nvm = false;

static long perf_event_open(struct perf_event_attr *hw_event, pid_t pid,
    int cpu, int group_fd, unsigned long flags)
{
  int ret;

  ret = syscall(__NR_perf_event_open, hw_event, pid, cpu,
		group_fd, flags);
  return ret;
}

static struct perf_event_mmap_page* perf_setup(__u64 config, __u64 config1, __u64 cpu, __u64 type)
{
  struct perf_event_attr attr;

  memset(&attr, 0, sizeof(struct perf_event_attr));

  attr.type = PERF_TYPE_RAW;
  attr.size = sizeof(struct perf_event_attr);

  attr.config = config;
  attr.config1 = config1;
  if (type == WRITE) {
    attr.sample_period = WRITE_SAMPLE_PERIOD;
  }
  else {
    attr.sample_period = SAMPLE_PERIOD;
  }

  attr.sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_ADDR;
  attr.pinned = 1;
  attr.disabled = 0;
  //attr.inherit = 1;
  attr.exclude_kernel = 1;
  attr.exclude_hv = 1;
  attr.exclude_callchain_kernel = 1;
  attr.exclude_callchain_user = 1;
  attr.precise_ip = 1;

  pfd[cpu][type] = perf_event_open(&attr, -1, cpu, -1, 0);
  if(pfd[cpu][type] == -1) {
    perror("perf_event_open");
  }
  assert(pfd[cpu][type] != -1);

  size_t mmap_size = sysconf(_SC_PAGESIZE) * PERF_PAGES;
  /* printf("mmap_size = %zu\n", mmap_size); */
  struct perf_event_mmap_page *p = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, pfd[cpu][type], 0);
  if(p == MAP_FAILED) {
    perror("mmap");
  }
  assert(p != MAP_FAILED);

  return p;
}

void *pebs_scan_thread()
{
#ifdef SAMPLE_BASED_COOLING
  uint64_t samples_since_cool = 0;
#endif

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

  for(;;) {
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
      for(int j = 0; j < NPBUFTYPES; j++) {
        struct perf_event_mmap_page *p = perf_page[i][j];
        char *pbuf = (char *)p + p->data_offset;

        __sync_synchronize();

        if(p->data_head == p->data_tail) {
          continue;
        }

        struct perf_event_header *ph = (void *)(pbuf + (p->data_tail % p->data_size));
        struct perf_sample* ps;
        struct hemem_page* page;

        switch(ph->type) {
        case PERF_RECORD_SAMPLE:
            ps = (struct perf_sample*)ph;
            assert(ps != NULL);
            if(ps->addr != 0) {
              __u64 pfn = ps->addr & HUGE_PFN_MASK;
              page = pebs_find_page(pfn);
              if (page != NULL) {
                if (page->va != 0) {
                  page->accesses[j][curr_access_version]++;
                }
                hemem_pages_cnt++;
                nvm_page_accesses_curr += (j == NVMREAD) ? 1 : 0;
              } else {
                other_pages_cnt++;
              }
              total_pages_cnt++;
            }
            else {
              zero_pages_cnt++;
            }
  	      break;
        case PERF_RECORD_THROTTLE:
        case PERF_RECORD_UNTHROTTLE:
          //fprintf(stderr, "%s event!\n", ph->type == PERF_RECORD_THROTTLE ? "THROTTLE" : "UNTHROTTLE");
          if (ph->type == PERF_RECORD_THROTTLE) {
              throttle_cnt++;
          }
          else {
              unthrottle_cnt++;
          }
          break;
        default:
          fprintf(stderr, "Unknown type %u\n", ph->type);
          //assert(!"NYI");
          break;
        }

        p->data_tail += ph->size;
      }
    }
  }

  return NULL;
}

static void pebs_migrate_down(struct hemem_page *page, uint64_t offset)
{
  struct timeval start, end;

  gettimeofday(&start, NULL);

  page->migrating = true;
  hemem_wp_page(page, true);
  hemem_migrate_down(page, offset);
  page->migrating = false;

  gettimeofday(&end, NULL);
  LOG_TIME("migrate_down: %f s\n", elapsed(&start, &end));
}

static void pebs_migrate_up(struct hemem_page *page, uint64_t offset)
{
  struct timeval start, end;

  gettimeofday(&start, NULL);

  page->migrating = true;
  hemem_wp_page(page, true);
  hemem_migrate_up(page, offset);
  page->migrating = false;

  gettimeofday(&end, NULL);
  LOG_TIME("migrate_up: %f s\n", elapsed(&start, &end));
}

// Sorts in ascending order
int ulong_sort_cmp(const void *a, const void *b) {
  uint64_t _a = *(const uint64_t *)a;
  uint64_t _b = *(const uint64_t *)b;
  return (_a < _b) ? -1 : (_a > _b);
}

// Sorts in descending order
int sort_entry_cmp(const void *a, const void *b) {
  struct score_entry _a = *(const struct score_entry*)a;
  struct score_entry _b = *(const struct score_entry*)b;

  return (_a.score > _b.score) ? -1 : (_a.score < _b.score);
}

static void reset_page_access_fields(struct hemem_page *page)
{
  for (int i = 0; i < NPBUFTYPES; i++) {
    page->accesses[i][0] = 0;
    page->accesses[i][1] = 0;
    page->s_accesses[i] = 0;
  }
  for (int i = 0; i < WINDOW_SIZE; i++) {
    page->w[i] = 0;
  }
}

static inline void update_window(struct hemem_page* page) {
  uint32_t accesses = page->s_accesses[DRAMREAD] + page->s_accesses[NVMREAD] + page->s_accesses[WRITE];
  for (int i = 0; i < WINDOW_SIZE; i++) {
    page->w[i] = (1. - w_ewma_alpha[i]) * page->w[i] + (w_ewma_alpha[i] * accesses);
  }
}

static inline float compute_score(const struct hemem_page *page, const uint8_t *bias) {
  // Update the score (average of the window)
  // TODO: Use a weighted average instead of a simple average
  uint32_t score = 0;
  for (int i = 0; i < WINDOW_SIZE; i++) {
    score += page->w[i] * bias[i];
  }
  return score;
}

static inline uint64_t _moving_avg_add(uint64_t avg, uint32_t new_val, uint32_t count) {
  return ((count * avg) + new_val) / (count + 1);
}
static inline void moving_avg_add(uint64_t* avg, struct hemem_page* page, size_t* count) {
  avg[DRAMREAD] = _moving_avg_add(avg[DRAMREAD], page->accesses[DRAMREAD][prev_access_version], *count);
  avg[NVMREAD] = _moving_avg_add(avg[NVMREAD], page->accesses[NVMREAD][prev_access_version], *count);
  avg[WRITE] = _moving_avg_add(avg[WRITE], page->accesses[WRITE][prev_access_version], *count);
  (*count)++;
}

static inline uint64_t _moving_avg_sub(uint64_t avg, uint32_t old_val, uint32_t count) {
  return ((count * avg) - old_val) / (count - 1);
}
static inline void moving_avg_sub(uint64_t* avg, struct hemem_page* page, size_t* count) {
  avg[DRAMREAD] = _moving_avg_sub(avg[DRAMREAD], page->accesses[DRAMREAD][prev_access_version], *count);
  avg[NVMREAD] = _moving_avg_sub(avg[NVMREAD], page->accesses[NVMREAD][prev_access_version], *count);
  avg[WRITE] = _moving_avg_sub(avg[WRITE], page->accesses[WRITE][prev_access_version], *count);
  (*count)--;
}

static size_t calculate_scores(struct score_entry *scores_out, const uint8_t *bias)
{
  struct ptimer window_timer, smooth_timer;
  ptimer_init(&window_timer, "Scores (window)");
  ptimer_init(&smooth_timer, "Scores (smooth)");

  struct hemem_page *page, *p;
  size_t n_idx;
  uint64_t n_va, va;
  kbitr_t itr, n_itr;
  page_tree_entry_t entry, *entry_ptr, *n_entry_ptr;

  size_t idx = 0;
  size_t s_idx = 0;
  size_t pages_cnt = kb_size(pages_tree);

  uint64_t smooth_avg_v[NPBUFTYPES];
  size_t smooth_avg_cnt = 0;
  memset(smooth_avg_v, 0, sizeof(smooth_avg_v));

#ifdef PAGE_ACCESS_SMOOTHING
  // Clear ring buffers
  ring_buf_reset(l_neighbours);
  ring_buf_reset(r_neighbours);
#endif

  // Init the (right) neightbour iterator
  kb_itr_first(kPagesTree, pages_tree, &n_itr);
  assert(kb_itr_valid(&n_itr));
  kb_itr_next(kPagesTree, pages_tree, &n_itr);

  // Iterate over the pages, in ascending order of VA
  kb_itr_first(kPagesTree, pages_tree, &itr);
  for (;
      kb_itr_valid(&itr);
      kb_itr_next(kPagesTree, pages_tree, &itr), idx++
  ) {
    assert(idx < pages_cnt);

    entry_ptr = &kb_itr_key(page_tree_entry_t, &itr);
    page = entry_ptr->page;
    if (page == NULL || !page->present) {
      continue;
    }

#ifdef PAGE_ACCESS_SMOOTHING
    ptimer_continue(&smooth_timer);
    //printf("Before smoothing\n");

    // Pop left neighbour(s)
    //printf("-> LEFT NEIGHBOURS\n");
    while(ring_buf_size(l_neighbours) > 0) {
      n_idx = idx - ring_buf_size(l_neighbours);
      p = (struct hemem_page*)ring_buf_peek_tail(l_neighbours, 0);
      if (p->va == page->va - ((idx - n_idx) * HUGEPAGE_SIZE)) {
        break;
      }
      // Remove neighbour from left neighbours
      moving_avg_sub(smooth_avg_v, p, &smooth_avg_cnt);
      ring_buf_get(l_neighbours);
    }
    assert(ring_buf_size(l_neighbours) >= 0 && ring_buf_size(l_neighbours) <= NUM_NEIGHBOURS);

    // Append right neighbour(s)
    //printf("-> RIGHT NEIGHBOURS\n");
    n_idx = idx + ring_buf_size(r_neighbours) + 1;
    while(ring_buf_size(r_neighbours) < NUM_NEIGHBOURS) {
      if (!kb_itr_valid(&n_itr)) {
        break; // no more neighbours
      }
      // Check if the next neighbour exists
      n_va = page->va + ((n_idx - idx) * HUGEPAGE_SIZE);
      n_entry_ptr = &kb_itr_key(page_tree_entry_t, &n_itr);
      p = n_entry_ptr->page;
      assert(p != NULL);
      assert(p->va == n_entry_ptr->va);
      if (p->va != n_va) {
        break;
      }
      // Add neighbour to right neighbours
      ring_buf_put(r_neighbours, (uint64_t*)p);
      moving_avg_add(smooth_avg_v, p, &smooth_avg_cnt);
      // Move to the next neighbour
      kb_itr_next(kPagesTree, pages_tree, &n_itr);
      n_idx++;
    }
    assert(ring_buf_size(r_neighbours) >= 0 && ring_buf_size(r_neighbours) <= NUM_NEIGHBOURS);

    // Add this page accesses to the moving average
    moving_avg_add(smooth_avg_v, page, &smooth_avg_cnt);

    // Calculate smoothed access count
    page->s_accesses[DRAMREAD] = smooth_avg_v[DRAMREAD];
    page->s_accesses[NVMREAD] = smooth_avg_v[NVMREAD];
    page->s_accesses[WRITE] = smooth_avg_v[WRITE];

    ptimer_stop(&smooth_timer);
    //printf("After smoothing\n");

    // Update the window with the smoothed access count
    update_window(page);

    // Calculate the hotness score
    page->score = compute_score(page, bias);
    scores_out[s_idx++] = (struct score_entry){ page, page->score };

    // Append this page to the left neighbours
    ring_buf_put(l_neighbours, (uint64_t*)page);

    // If the left neighbours buffer is full, pop the leftmost neighbour
    if (ring_buf_size(l_neighbours) > NUM_NEIGHBOURS) {
      p = (struct hemem_page*)ring_buf_get(l_neighbours);
      moving_avg_sub(smooth_avg_v, p, &smooth_avg_cnt);
    }
    // Pop the leftmost neighbour in right neighbours buffer
    // This is soon-to-be the next page (i.e., the right neighbour)
    if (ring_buf_size(r_neighbours) > 0) {
      ring_buf_get(r_neighbours);
    }
#else
    // Calculate smoothed access count
    page->s_accesses[DRAMREAD] = page->accesses[DRAMREAD][prev_access_version];
    page->s_accesses[NVMREAD] = page->accesses[NVMREAD][prev_access_version];
    page->s_accesses[WRITE] = page->accesses[WRITE][prev_access_version];
    fprintf(fa, "%lu,%f|", page->va, page->s_accesses[DRAMREAD] + page->s_accesses[NVMREAD]); //+ page->s_accesses[WRITE]);

    // Update the window with the smoothed access count
    update_window(page);

    // Calculate the hotness score
    page->score = compute_score(page, bias);
    scores_out[s_idx++] = (struct score_entry){ page, page->score };

    fprintf(fs, "%lu,%f|", page->va, page->score);
#endif

  }
  ptimer_print(&smooth_timer);

  return s_idx;
}

int continue_migration(struct hemem_page *hp, struct hemem_page *cp, uint32_t migrated_pages)
{
  //if (hp->score < 1.20 * cp->score) {
  if (migrated_pages >= 10) {
    return 0;
  }
  return 1;
}

void promote_to_free_dram_page(struct hemem_page *p, struct hemem_page *np)
{
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

void demote_to_free_nvm_page(struct hemem_page *cp, struct hemem_page *np)
{
  uint64_t old_offset;

  // There could be a possible race with pebs_remove_page()
  // So acquire lock to ensure page is not removed while being migrated
  pthread_mutex_lock(&(cp->page_lock));
  if (!cp->present) {
    // Don't migrate as this page is being removed
    // Put np back on nvm_free_list
    enqueue_fifo(&nvm_free_list, np);
    pthread_mutex_unlock(&(cp->page_lock));
    return;
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
  //enqueue_fifo(&dram_free_list, np);
}

void *pebs_policy_thread()
{
  struct ptimer loop_timer, tree_timer, score_timer, sort_timer, id_timer, migrate_timer;
  ptimer_init(&loop_timer, "Loop");
  ptimer_init(&tree_timer, "Tree");
  ptimer_init(&score_timer, "Score");
  ptimer_init(&sort_timer, "Sort");
  ptimer_init(&id_timer, "Identify");
  ptimer_init(&migrate_timer, "Migrate");

  cpu_set_t cpuset;
  pthread_t thread;
  //int tries;
  struct hemem_page *p;
  struct hemem_page *cp;
  struct hemem_page *np;
  uint64_t migrated_bytes;
  //uint64_t old_offset;
  double migrate_time_us;
  struct hemem_page* page = NULL;

  page_tree_entry_t entry;

  size_t pages_cnt, s_pages_cnt;

  // Use a dedicated CPU core for the policy thread
  thread = pthread_self();
  CPU_ZERO(&cpuset);
  CPU_SET(MIGRATION_THREAD_CPU, &cpuset);
  int s = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
  if (s != 0) {
    perror("pthread_setaffinity_np");
    assert(0);
  }

  // Sleep first to allow the scanning thread to start
  usleep((uint64_t)((1.0 * PEBS_KSWAPD_INTERVAL)));

  for (;;) {
    ptimer_start(&loop_timer);

    fprintf(stderr, "\n========================================\n");
    fprintf(stderr, "Starting new interval\n");
    fprintf(stderr, "========================================\n");

    // Update the window index (circular buffer)
    curr_window_index = global_version % WINDOW_SIZE;
    // "Bump" the global version to indicate that we are starting a new interval
    global_version++;
    prev_access_version = curr_access_version;
    curr_access_version = 1 - curr_access_version;
    __sync_synchronize();

    // Update the NVM page access smoothing
    nvm_page_accesses_prev = nvm_page_accesses_curr;
    nvm_page_accesses_curr = 0;
    __sync_synchronize();
    // Compute peak-to-average ratio
    float ratio = (float)(nvm_page_accesses_prev) / nvm_page_ewma;
    nvm_page_ewma = 0.9 * nvm_page_ewma + 0.1 * nvm_page_accesses_prev;
    // Update the bias
    uint8_t* bias = (ratio > 1.0) ? recn_bias : hist_bias;
    printf("NVM page accesses: %u, NVM page accesses EWMA: %f, Ratio: %f\n", nvm_page_accesses_prev, nvm_page_ewma, ratio);

    // free pages using free page ring buffer
    ptimer_start(&tree_timer);
    while (true) {
      mod_page_t* mp;
      pthread_mutex_lock(&mod_page_dq_lock);
      if (kdq_size(mod_page_dq) == 0) {
        pthread_mutex_unlock(&mod_page_dq_lock);
        break;
      }
      mp = kdq_shift(mod_page_t, mod_page_dq);
      pthread_mutex_unlock(&mod_page_dq_lock);

      page = mp->page;

      //fprintf(stderr, "Processing page %lu [va: %lu]\n", page, page->va);
      entry.page = page;
      entry.va = page->va;

      if (mp->free) {
        kb_del(kPagesTree, pages_tree, entry);

        // Add page to correct free list
        if (page->in_dram) {
          enqueue_fifo(&dram_free_list, page);
        } else {
          enqueue_fifo(&nvm_free_list, page);
        }
        reset_page_access_fields(page);

      } else {
        kb_put(kPagesTree, pages_tree, entry);
      }
    }
    ptimer_stop_and_print(&tree_timer);

    // Calculate the scores
    ptimer_start(&score_timer);
    pages_cnt = kb_size(pages_tree);
    s_pages_cnt = calculate_scores(scores, bias);
    fprintf(stderr, "pages_cnt: %lu, s_pages_cnt: %lu\n", pages_cnt, s_pages_cnt);
    ptimer_stop_and_print(&score_timer);

    // Sort the scores (in descending order)
    ptimer_start(&sort_timer);
    qsort(scores, s_pages_cnt, sizeof(struct score_entry), sort_entry_cmp);
    ptimer_stop_and_print(&sort_timer);

    // Perform migrations
    ptimer_reset(&id_timer);
    ptimer_reset(&migrate_timer);

    int64_t promote_idx = 0;
    int64_t demote_idx = s_pages_cnt - 1;
    size_t migrated_pages = 0;

    //printf("Promote idx: %lu, Demote idx: %lu\n", promote_idx, demote_idx);

    migrated_bytes = 0;
    while (promote_idx < demote_idx) {
      // find the hotest NVM page that needs to be promoted
      ptimer_continue(&id_timer);
      while (promote_idx < demote_idx && scores[promote_idx].page->in_dram) {
        promote_idx++;
      }
      if (promote_idx >= demote_idx) {
        break;
      }
      p = scores[promote_idx].page;
      //printf("Promoting page %lu [idx %lu] with score %f\n", p, promote_idx, scores[promote_idx].score);
      assert(!p->in_dram);

      // try to find a free DRAM page
      np = dequeue_fifo(&dram_free_list);
      if (np != NULL) {
        assert(!(np->present));
        ptimer_stop(&id_timer);

        //printf("Free DRAM page found: %p\n", np);
        ptimer_continue(&migrate_timer);
        promote_to_free_dram_page(p, np);
        migrated_bytes += pt_to_pagesize(p->pt);
        migrated_pages++;
        ptimer_stop(&migrate_timer);
        continue;
      }

      // Find the coldest DRAM page that needs to be demoted
      while (demote_idx > promote_idx && !scores[demote_idx].page->in_dram) {
        demote_idx--;
      }
      if (demote_idx <= promote_idx) {
        break;
      }

      cp = scores[demote_idx].page;
      //printf("Demoting page %p [idx %lu] with score %f\n", cp, demote_idx, scores[demote_idx].score);
      assert(cp->in_dram && cp->va > 0);

      if (!continue_migration(p, cp, migrated_pages)) {
        break;
      }
      //printf("Promote score %f, demote score %f\n", scores[promote_idx].score, scores[demote_idx].score);

      // try to find a free NVM page
      np = dequeue_fifo(&nvm_free_list);
      assert(np != NULL);
      //printf("Free NVM page found: %p\n", np);
      ptimer_stop(&id_timer);

      // move the cold DRAM page to NVM
      ptimer_continue(&migrate_timer);
      //printf("Demote page %p to free NVM page %p\n", cp, np);
      demote_to_free_nvm_page(cp, np);
      migrated_bytes += pt_to_pagesize(cp->pt);

      // move the hot NVM page to the (now-free) DRAM page
      //printf("Promote page %p to free DRAM page %p\n", p, cp);
      promote_to_free_dram_page(p, np);
      migrated_bytes += pt_to_pagesize(p->pt);

      migrated_pages += 2;

      promote_idx++;
      demote_idx--;

      ptimer_stop(&migrate_timer);
    }

    ptimer_print(&id_timer);
    ptimer_print(&migrate_timer);
    ptimer_stop_and_print(&loop_timer);

    fprintf(stderr, "Migrated %lu pages (%lu bytes) in this interval\n", migrated_pages, migrated_bytes);
    migrate_time_us = loop_timer.elapsed_us;
    if (migrate_time_us < (1.0 * PEBS_KSWAPD_INTERVAL)) {
      //printf("%lu", ((uint64_t)((1.0 * PEBS_KSWAPD_INTERVAL) - migrate_time_us)));
      usleep((uint64_t)((1.0 * PEBS_KSWAPD_INTERVAL) - migrate_time_us));
    }
  }

  return NULL;
}

static struct hemem_page* pebs_allocate_page()
{
  struct timeval start, end;
  struct hemem_page *page;

  gettimeofday(&start, NULL);
  page = dequeue_fifo(&dram_free_list);
  if (page != NULL) {
    assert(page->in_dram);
    assert(!page->present);

    page->present = true;
    //enqueue_fifo(&dram_cold_list, page);

    gettimeofday(&end, NULL);
    LOG_TIME("mem_policy_allocate_page: %f s\n", elapsed(&start, &end));

    return page;
  }

  // DRAM is full, fall back to NVM
  page = dequeue_fifo(&nvm_free_list);
  if (page != NULL) {
    assert(!page->in_dram);
    assert(!page->present);

    page->present = true;
    //enqueue_fifo(&nvm_cold_list, page);

    gettimeofday(&end, NULL);
    LOG_TIME("mem_policy_allocate_page: %f s\n", elapsed(&start, &end));

    return page;
  }

  assert(!"Out of memory");
}

struct hemem_page* pebs_pagefault(void)
{
  struct hemem_page *page;

  // do the heavy lifting of finding the devdax file offset to place the page
  page = pebs_allocate_page();
  assert(page != NULL);

  return page;
}

void pebs_add_page(struct hemem_page *page)
{
  struct hemem_page *p;
  assert(page != NULL);
  LOG("pebs: add page, put this page into add_pages_ring: va: 0x%lx\n", page->va);

  //printf("Adding page %lu to the add_pages_ring [va: %lu]\n", (uint64_t)page, page->va);

  // Add to the hash table
  pthread_mutex_lock(&pages_lock);
  HASH_FIND(hh, pages, &(page->va), sizeof(uint64_t), p);
  assert(p == NULL);
  HASH_ADD(hh, pages, va, sizeof(uint64_t), page);
  pthread_mutex_unlock(&pages_lock);

  // Add to the new pages ring
  pthread_mutex_lock(&mod_page_dq_lock);
  mod_page_t mp = (mod_page_t){ .page = page, .free = false };
  kdq_push(mod_page_t, mod_page_dq, mp);
  pthread_mutex_unlock(&mod_page_dq_lock);
}

struct hemem_page* pebs_find_page(uint64_t va)
{
  struct hemem_page *page;
  pthread_mutex_lock(&pages_lock);
  HASH_FIND(hh, pages, &va, sizeof(uint64_t), page);
  pthread_mutex_unlock(&pages_lock);
  return page;
}

void pebs_remove_page(struct hemem_page *page)
{
  assert(page != NULL);
  LOG("pebs: remove page, put this page into free_page_ring: va: 0x%lx\n", page->va);

  //printf("Removing page %lu from the add_pages_ring [va: %lu]\n", (uint64_t)page, page->va);

  // Remove page from hash table
  pthread_mutex_lock(&pages_lock);
  HASH_DEL(pages, page);
  pthread_mutex_unlock(&pages_lock);

  pthread_mutex_lock(&mod_page_dq_lock);
  mod_page_t mp = (mod_page_t){ .page = page, .free = true};
  kdq_push(mod_page_t, mod_page_dq, mp);
  pthread_mutex_unlock(&mod_page_dq_lock);

  // We set page->present to false so that
  // the migration thread does not migrate this page
  pthread_mutex_lock(&(page->page_lock));
  page->present = false;
  pthread_mutex_unlock(&(page->page_lock));
}

#ifdef SCAILP
#define L3_LOAD_MISS_LOCAL 0x2d3
#define L3_LOAD_MISS_REMOTE 0x10d3
#elif defined JOSEPM
#define L3_LOAD_MISS_LOCAL 0x1d3
#define L3_LOAD_MISS_REMOTE 0x80d1
#elif defined C220G5
#define L3_LOAD_MISS_LOCAL 0x1d3
#define L3_LOAD_MISS_REMOTE 0x2d3
#endif

void pebs_init(void)
{
  pthread_t kswapd_thread;
  pthread_t scan_thread;
  uint64_t** buffer;

  LOG("pebs_init: started\n");

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
    //perf_page[i][READ] = perf_setup(0x1cd, 0x4, i);  // MEM_TRANS_RETIRED.LOAD_LATENCY_GT_4
    //perf_page[i][READ] = perf_setup(0x81d0, 0, i);   // MEM_INST_RETIRED.ALL_LOADS
    perf_page[i][DRAMREAD] = perf_setup(L3_LOAD_MISS_LOCAL, 0, i, DRAMREAD);      // MEM_LOAD_L3_MISS_RETIRED.LOCAL_DRAM
    perf_page[i][NVMREAD] = perf_setup(L3_LOAD_MISS_REMOTE, 0, i, NVMREAD);     // MEM_LOAD_RETIRED.LOCAL_PMM
    perf_page[i][WRITE] = perf_setup(0x82d0, 0, i, WRITE);    // MEM_INST_RETIRED.ALL_STORES
    //perf_page[i][WRITE] = perf_setup(0x12d0, 0, i);   // MEM_INST_RETIRED.STLB_MISS_STORES
  }

  pthread_mutex_init(&(dram_free_list.list_lock), NULL);
  for (int i = 0; i < dramsize / PAGE_SIZE; i++) {
    struct hemem_page *p = calloc(1, sizeof(struct hemem_page));
    p->devdax_offset = i * PAGE_SIZE;
    p->present = false;
    p->in_dram = true;
    p->pt = pagesize_to_pt(PAGE_SIZE);
    pthread_mutex_init(&(p->page_lock), NULL);

    enqueue_fifo(&dram_free_list, p);
  }

  pthread_mutex_init(&(nvm_free_list.list_lock), NULL);
  for (int i = 0; i < nvmsize / PAGE_SIZE; i++) {
    struct hemem_page *p = calloc(1, sizeof(struct hemem_page));
    p->devdax_offset = i * PAGE_SIZE;
    p->present = false;
    p->in_dram = false;
    p->pt = pagesize_to_pt(PAGE_SIZE);
    pthread_mutex_init(&(p->page_lock), NULL);

    enqueue_fifo(&nvm_free_list, p);
  }

  //pthread_mutex_init(&(dram_hot_list.list_lock), NULL);
  //pthread_mutex_init(&(dram_cold_list.list_lock), NULL);
  //pthread_mutex_init(&(nvm_hot_list.list_lock), NULL);
  //pthread_mutex_init(&(nvm_cold_list.list_lock), NULL);

  // Initialize the UT_array for tracking active pages
  pages_tree = kb_init(kPagesTree, KB_DEFAULT_SIZE);

  //buffer = (uint64_t**)malloc(sizeof(uint64_t*) * MAX_NVME_PAGES);
  //assert(buffer);
  //promote_page_ring = ring_buf_init(buffer, MAX_NVME_PAGES);
  //buffer = (uint64_t**)malloc(sizeof(uint64_t*) * MAX_DRAM_PAGES);
  //assert(buffer);
  //demote_page_ring = ring_buf_init(buffer, MAX_DRAM_PAGES);

  scores = (struct score_entry*)malloc((MAX_NVME_PAGES + MAX_DRAM_PAGES) * sizeof(struct score_entry));

  // Initialize the free/add ring buffers
  mod_page_dq = kdq_init(mod_page_t);

  // Initialize the neighbour ring buffers
  buffer = (uint64_t**)malloc(sizeof(uint64_t*) * (NUM_NEIGHBOURS + 2));
  assert(buffer);
  l_neighbours = ring_buf_init(buffer, NUM_NEIGHBOURS + 2);
  buffer = (uint64_t**)malloc(sizeof(uint64_t*) * (NUM_NEIGHBOURS + 2));
  assert(buffer);
  r_neighbours = ring_buf_init(buffer, NUM_NEIGHBOURS + 2);

  // Initialize bias values
  for (int i = 0; i < WINDOW_SIZE; i++) {
    w_ewma_alpha[i] = 2.0/((1 << (i + 1)) + 1);
    hist_bias[i] = 1;
    recn_bias[i] = 32 / (i + 1);
    assert(recn_bias[i] > 0);
  }
  for (int i = 0; i < WINDOW_SIZE; i++) {
    printf("w_ewma_alpha[%d] = %f\n", i, w_ewma_alpha[i]);
  }
  for (int i = 0; i < WINDOW_SIZE; i++) {
    printf("hist_bias[%d] = %d\n", i, hist_bias[i]);
  }
  for (int i = 0; i < WINDOW_SIZE; i++) {
    printf("recn_bias[%d] = %d\n", i, recn_bias[i]);
  }

  // Start the policy and scan threads
  int r = pthread_create(&scan_thread, NULL, pebs_scan_thread, NULL);
  assert(r == 0);

  r = pthread_create(&kswapd_thread, NULL, pebs_policy_thread, NULL);
  assert(r == 0);

  LOG("Memory management policy is PEBS\n");

  LOG("pebs_init: finished\n");

}

void pebs_shutdown()
{
  for (int i = 0; i < PEBS_NPROCS; i++) {
    for (int j = 0; j < NPBUFTYPES; j++) {
      ioctl(pfd[i][j], PERF_EVENT_IOC_DISABLE, 0);
      //munmap(perf_page[i][j], sysconf(_SC_PAGESIZE) * PERF_PAGES);
    }
  }
}

void pebs_stats()
{
  //LOG_STATS("dram_hot_list:[%ld] dram_cold_list:[%ld] nvm_hot_list:[%ld] nvm_cold_list:[%ld] samples:[%ld/%ld] throttle/unthrottle_cnt:[%ld/%ld] cools:[%ld]\n",
  LOG_STATS("samples:[%ld/%ld] throttle/unthrottle_cnt:[%ld/%ld] cools:[%ld]\n",
          //dram_hot_list.numentries,
          //dram_cold_list.numentries,
          //nvm_hot_list.numentries,
          //nvm_cold_list.numentries,
          hemem_pages_cnt,
          total_pages_cnt,
          throttle_cnt,
          unthrottle_cnt,
          cools);
  // hemem_pages_cnt = total_pages_cnt =  throttle_cnt = unthrottle_cnt = 0;
}
