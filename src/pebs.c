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

#include "kbtree.h"
#include "uthash.h"

// Hash table for Hemem-handled pages
struct hemem_page *pages = NULL;
pthread_mutex_t pages_lock = PTHREAD_MUTEX_INITIALIZER;

#define ktree_cmp(a,b) ( (a) < (b) ? -1 : (a) > (b) )
KBTREE_INIT(kPagesTree, uint64_t, ktree_cmp)
kbtree_t(kPagesTree) *pages_tree;

//static struct fifo_list dram_hot_list;
//static struct fifo_list dram_cold_list;
//static struct fifo_list nvm_hot_list;
//static struct fifo_list nvm_cold_list;

static struct fifo_list dram_free_list;
static struct fifo_list nvm_free_list;

//static ring_handle_t promote_page_ring;
//static ring_handle_t demote_page_ring;

// Pages to be freed/added in the next interval
static ring_handle_t free_page_ring;
static pthread_mutex_t free_page_ring_lock = PTHREAD_MUTEX_INITIALIZER;
static ring_handle_t add_pages_ring;
static pthread_mutex_t add_pages_ring_lock = PTHREAD_MUTEX_INITIALIZER;

// Neighbour buffers
static ring_handle_t l_neighbours;
static ring_handle_t r_neighbours;

volatile uint64_t global_version = 0;

volatile uint8_t curr_access_version = 0;
volatile uint8_t prev_access_version; // = 1 - curr_access_version

volatile uint8_t curr_window_index = 0;
volatile uint8_t prev_window_version;



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
          //fprintf(stderr, "%s event!\n",
          //   ph->type == PERF_RECORD_THROTTLE ? "THROTTLE" : "UNTHROTTLE");
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

void reset_page_access_fields(struct hemem_page *page)
{
  for (int i = 0; i < NPBUFTYPES; i++) {
    page->accesses[i][0] = 0;
    page->accesses[i][1] = 0;
    page->s_accesses[i] = 0;
  }
  for (int i = 0; i < WINDOW_SIZE; i++) {
    page->w[i] = 0;
    page->w_size = 0;
  }
}

inline uint32_t compute_window_value(struct hemem_page* page) {
  // TODO: Weight the read/write accesses differently
  return page->s_accesses[DRAMREAD] + page->s_accesses[NVMREAD] + page->s_accesses[WRITE];
}

inline float compute_score(struct hemem_page *page) {
  // Update the score (average of the window)
  // TODO: Use a weighted average instead of a simple average
  int index = curr_window_index;
  int w_size = page->w_size;

  float score = 0;
  while (w_size > 0) {
    score += page->w[index];
    index = (index - 1) % WINDOW_SIZE;
    w_size--;
  }
  score /= page->w_size;

  return score;
}


void calculate_scores(struct score_entry *scores)
{
  struct hemem_page *page, *p;
  size_t idx, n_idx, pages_cnt;
  uint64_t n_va;
  kbitr_t itr;

  // Smoothing phase
  ring_buf_reset(l_neighbours);
  ring_buf_reset(r_neighbours);

  idx = 0;
  pages_cnt = kb_size(pages_tree);

  kb_itr_first(kPagesTree, pages_tree, &itr);
  for (;
      kb_itr_valid(&itr);
      kb_itr_next(kPagesTree, pages_tree, &itr), idx++
  ) {
    assert(idx < pages_cnt);
    page = (struct hemem_page*)pebs_find_page(kb_itr_key(uint64_t, &itr));

    // Pop left neighbour(s)
    while(ring_buf_size(l_neighbours) > 0) {
      n_idx = idx - ring_buf_size(l_neighbours);
      p = (struct hemem_page*)ring_buf_peek(l_neighbours, 0); // Peek leftmost neighbour
      if (p->va != page->va - ((idx - n_idx) * HUGEPAGE_SIZE)) {
        ring_buf_get(l_neighbours);
      } else {
        break;
      }
    }
    // Append right neighbour(s)
    n_idx = idx + 1;
    while(ring_buf_size(r_neighbours) < NUM_NEIGHBOURS) {
      if (n_idx >= pages_cnt) {
        break;
      }
      // Check if the next neighbour exists
      n_va = page->va + ((n_idx - idx) * HUGEPAGE_SIZE);
      p = (struct hemem_page*)pebs_find_page(n_va);
      if (p == NULL) {
        break;
      }
      // Add neighbour to right neighbours
      ring_buf_put(r_neighbours, (uint64_t*)p);
      n_idx++;
    }

    // Calculate smoothed access count
    for (int i = 0; i < NPBUFTYPES; i++) {
      page->s_accesses[i] = 0;
    }
    for (size_t nj = 0; nj < ring_buf_size(l_neighbours); nj++) {
      for (int i = 0; i < NPBUFTYPES; i++) {
        p = (struct hemem_page*)ring_buf_peek(l_neighbours, nj);
        page->s_accesses[i] += p->accesses[i][prev_access_version];
      }
    }
    for (size_t nj = 0; nj < ring_buf_size(r_neighbours); nj++) {
      for (int i = 0; i < NPBUFTYPES; i++) {
        p = (struct hemem_page*)ring_buf_peek(r_neighbours, nj);
        page->s_accesses[i] += p->accesses[i][prev_access_version];
      }
    }
    for (int i = 0; i < NPBUFTYPES; i++) {
      page->s_accesses[i] += page->accesses[i][prev_access_version];
      page->s_accesses[i] /= (ring_buf_size(l_neighbours) + ring_buf_size(r_neighbours) + 1);
    }
    // Update the window with the smoothed access count
    page->w[curr_window_index] = compute_window_value(page);
    page->w_size = (page->w_size < WINDOW_SIZE) ? page->w_size + 1 : WINDOW_SIZE;

    // Calculate the hotness score
    page->score = compute_score(page);
    scores[idx] = (struct score_entry){ page, page->score };

    // Append this page to the left neighbours
    ring_buf_put(l_neighbours, (uint64_t*)page);
    // If the left neighbours buffer is full, pop the leftmost neighbour
    if (ring_buf_size(l_neighbours) > NUM_NEIGHBOURS) {
      ring_buf_get(l_neighbours);
    }

    // Pop the rightmost neighbour if the right neighbours buffer is full
    if (ring_buf_size(r_neighbours) > NUM_NEIGHBOURS) {
      ring_buf_get(r_neighbours);
    }
  }
}

int continue_migration(struct hemem_page *hp, struct hemem_page *cp, uint32_t migrated_pages)
{
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
  cpu_set_t cpuset;
  pthread_t thread;
  struct timeval start, end;
  //int tries;
  struct hemem_page *p;
  struct hemem_page *cp;
  struct hemem_page *np;
  uint64_t migrated_bytes;
  //uint64_t old_offset;
  double migrate_time;
  struct hemem_page* page = NULL;

  uint64_t pages_cnt;
  struct score_entry *scores;

  // Use a dedicated CPU core for the policy thread
  thread = pthread_self();
  CPU_ZERO(&cpuset);
  CPU_SET(MIGRATION_THREAD_CPU, &cpuset);
  int s = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
  if (s != 0) {
    perror("pthread_setaffinity_np");
    assert(0);
  }

  for (;;) {
    gettimeofday(&start, NULL);

    // Update the window index (circular buffer)
    curr_window_index = global_version % WINDOW_SIZE;
    // "Bump" the global version to indicate that we are starting a new interval
    global_version++;
    prev_access_version = curr_access_version;
    curr_access_version = 1 - curr_access_version;
    __sync_synchronize();

    // free pages using free page ring buffer
    while(!ring_buf_empty(free_page_ring)) {
      page = (struct hemem_page*)ring_buf_get(free_page_ring);
      if (page == NULL) {
        continue;
      }

      // Remove page from hash table
      pthread_mutex_lock(&pages_lock);
      HASH_DEL(pages, page);
      pthread_mutex_unlock(&pages_lock);

      // Remove page from pages tree
      kb_del(kPagesTree, pages_tree, page->va);

      // Add page to correct free list
      if (page->in_dram) {
        enqueue_fifo(&dram_free_list, page);
      }
      else {
        enqueue_fifo(&nvm_free_list, page);
      }

      reset_page_access_fields(page);
    }
    // add pages to pages tree
    while(!ring_buf_empty(add_pages_ring)) {
      page = (struct hemem_page*)ring_buf_get(add_pages_ring);
      if (page == NULL) {
          continue;
      }
      kb_put(kPagesTree, pages_tree, page->va);
    }
    pages_cnt = kb_size(pages_tree);

    // TODO: Allocate mem for scores only once
    scores = (struct score_entry*)malloc(pages_cnt * sizeof(struct score_entry));
    calculate_scores(scores);

    // Sort the scores (in descending order)
    qsort(scores, pages_cnt, sizeof(float), sort_entry_cmp);

    /* I don't think this is needed actually
    // Move pages to the promote/demote ring buffers
    // TODO: optimize this using a single loop
    for (size_t i = 0; i < pages_cnt; i++) {
      p = scores[i].page;
      if (!p->in_dram && !ring_buf_full(promote_page_ring)) {
        ring_buf_put(promote_page_ring, (uint64_t*)p);
      }
    }
    for (size_t i = pages_cnt-1; i >= 0; i--) {
      p = scores[i].page;
      if (p->in_dram && !ring_buf_full(demote_page_ring)) {
        ring_buf_put(demote_page_ring, (uint64_t*)p);
      }
    }*/

    // Perform migration
    size_t promote_idx = 0;
    size_t demote_idx = pages_cnt - 1;
    size_t migrated_pages = 0;

    while (promote_idx < demote_idx) {
      // find the hotest NVM page that needs to be promoted
      while (promote_idx < demote_idx && !scores[promote_idx].page->in_dram) {
        promote_idx++;
      }
      if (promote_idx >= demote_idx) {
        break;
      }
      p = scores[promote_idx].page;

      // try to find a free DRAM page
      np = dequeue_fifo(&dram_free_list);
      if (np != NULL) {
        assert(!(np->present));
        promote_to_free_dram_page(p, np);
        migrated_bytes += pt_to_pagesize(p->pt);
        migrated_pages++;
        continue;
      }

      // Find the coldest DRAM page that needs to be demoted
      while (demote_idx > promote_idx && scores[demote_idx].page->in_dram) {
        demote_idx--;
      }
      if (demote_idx <= promote_idx) {
        break;
      }

      if (!continue_migration(scores[promote_idx].page, scores[demote_idx].page, migrated_pages)) {
        break;
      }

      cp = scores[demote_idx].page;
      assert(cp->va > 0);

      // try to find a free NVM page
      np = dequeue_fifo(&nvm_free_list);
      assert(np != NULL);

      // move the cold DRAM page to NVM
      demote_to_free_nvm_page(cp, np);
      migrated_bytes += pt_to_pagesize(cp->pt);

      // move the hot NVM page to the (now-free) DRAM page
      promote_to_free_dram_page(p, cp);
      migrated_bytes += pt_to_pagesize(p->pt);

      migrated_pages += 2;

      promote_idx++;
      demote_idx--;
    }


    // move each hot NVM page to DRAM
    /*
    for (migrated_bytes = 0; migrated_bytes < PEBS_KSWAPD_MIGRATE_RATE;) {
      p = dequeue_fifo(&nvm_hot_list);
      if (p == NULL) {
        // nothing in NVM is currently hot -- bail out
        break;
      }

      if ((p->accesses[WRITE] < HOT_WRITE_THRESHOLD) && (p->accesses[DRAMREAD] + p->accesses[NVMREAD] < HOT_READ_THRESHOLD)) {
        // it has been cooled, need to move it into the cold list
        enqueue_fifo(&nvm_cold_list, p);
        continue;
      }

      for (tries = 0; ; tries++) {
        // find a free DRAM page
        np = dequeue_fifo(&dram_free_list);

        if (np != NULL) {
          assert(!(np->present));

          LOG("%lx: cold %lu -> hot %lu\t slowmem.hot: %lu, slowmem.cold: %lu\t fastmem.hot: %lu, fastmem.cold: %lu\n",
                p->va, p->devdax_offset, np->devdax_offset, nvm_hot_list.numentries, nvm_cold_list.numentries, dram_hot_list.numentries, dram_cold_list.numentries);

          // There could be a possible race with pebs_remove_page()
          // So acquire lock to ensure page is not removed while being migrated
          pthread_mutex_lock(&(p->page_lock));
          if (!p->present) {
            // Don't migrate as this page is being removed
            // Put np back on the dram_free_list because we are not going to migrate
            enqueue_fifo(&dram_free_list, np);
            pthread_mutex_unlock(&(p->page_lock));
            break;
          } else {
            old_offset = p->devdax_offset;
            pebs_migrate_up(p, np->devdax_offset);
            // We can release the lock now that migration is complete
            pthread_mutex_unlock(&(p->page_lock));

            // Reset the page fields
            np->devdax_offset = old_offset;
            np->in_dram = false;
            np->present = false;
            reset_page_access_fields(np);

            enqueue_fifo(&dram_hot_list, p);
            enqueue_fifo(&nvm_free_list, np);

            migrated_bytes += pt_to_pagesize(p->pt);
            break;
          }
        }

        // no free dram page, try to find a cold dram page to move down
        cp = dequeue_fifo(&dram_cold_list);
        if (cp == NULL) {
          // all dram pages are hot, so put it back in list we got it from
          enqueue_fifo(&nvm_hot_list, p);
          goto out;
        } else if (cp->va == 0) {
          // This page is being allocated by the page fault handler.
          // It was moved from dram_free_list to dram_cold_list by pebs_allocate_page
          // Put it back on dram_cold_list and try again
          LOG("Retrying to find a dram cold page cause cur page is being allocated\n");
          enqueue_fifo(&dram_cold_list, cp);
          continue;
        }
        assert(cp != NULL);

        // find a free nvm page to move the cold dram page to
        np = dequeue_fifo(&nvm_free_list);
        if (np != NULL) {
          assert(!(np->present));

          LOG("%lx: hot %lu -> cold %lu\t slowmem.hot: %lu, slowmem.cold: %lu\t fastmem.hot: %lu, fastmem.cold: %lu\n",
                cp->va, cp->devdax_offset, np->devdax_offset, nvm_hot_list.numentries, nvm_cold_list.numentries, dram_hot_list.numentries, dram_cold_list.numentries);

          // There could be a possible race with pebs_remove_page()
          // So acquire lock to ensure page is not removed while being migrated
          pthread_mutex_lock(&(cp->page_lock));
          if (!cp->present) {
            // Don't migrate as this page is being removed
            pthread_mutex_unlock(&(cp->page_lock));
            // Put np back on nvm_free_list
            enqueue_fifo(&nvm_free_list, np);
            continue;
          } else {
            old_offset = cp->devdax_offset;
            pebs_migrate_down(cp, np->devdax_offset);
            pthread_mutex_unlock(&(cp->page_lock));

            // Reset the page fields
            np->devdax_offset = old_offset;
            np->in_dram = true;
            np->present = false;
            reset_page_access_fields(np);

            enqueue_fifo(&nvm_cold_list, cp);
            enqueue_fifo(&dram_free_list, np);
          }
        }
        assert(np != NULL);

        if (tries > 5)
          LOG("Tried %d times to find a free dram page\n", tries);
      }
    }
    */

//out:
  gettimeofday(&end, NULL);
    migrate_time = elapsed(&start, &end) * 1000000.0;
    LOG("migrate: %.2f us\n", migrate_time);
    if (migrate_time < (1.0 * PEBS_KSWAPD_INTERVAL)) {
      usleep((uint64_t)((1.0 * PEBS_KSWAPD_INTERVAL) - migrate_time));
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

  // Add to the hash table
  pthread_mutex_lock(&pages_lock);
  HASH_FIND(hh, pages, &(page->va), sizeof(uint64_t), p);
  assert(p == NULL);
  HASH_ADD(hh, pages, va, sizeof(uint64_t), page);
  pthread_mutex_unlock(&pages_lock);

  // Add to the new pages ring
  pthread_mutex_lock(&add_pages_ring_lock);
  while (ring_buf_full(add_pages_ring));
  ring_buf_put(add_pages_ring, (uint64_t*)page);
  pthread_mutex_unlock(&add_pages_ring_lock);
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

  // NOTE: Page will be removed from the hash table by the migration thread

  // Add to the free page ring buffer
  pthread_mutex_lock(&free_page_ring_lock);
  while (ring_buf_full(free_page_ring));
  ring_buf_put(free_page_ring, (uint64_t*)page);
  pthread_mutex_unlock(&free_page_ring_lock);

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

  // Initialize the free/add ring buffers
  buffer = (uint64_t**)malloc(sizeof(uint64_t*) * CAPACITY);
  assert(buffer);
  add_pages_ring = ring_buf_init(buffer, CAPACITY);
  buffer = (uint64_t**)malloc(sizeof(uint64_t*) * CAPACITY);
  assert(buffer);
  free_page_ring = ring_buf_init(buffer, CAPACITY);

  // Initialize the neighbour ring buffers
  buffer = (uint64_t**)malloc(sizeof(uint64_t*) * (NUM_NEIGHBOURS + 1));
  assert(buffer);
  l_neighbours = ring_buf_init(buffer, NUM_NEIGHBOURS + 1);
  buffer = (uint64_t**)malloc(sizeof(uint64_t*) * (NUM_NEIGHBOURS + 1));
  assert(buffer);
  r_neighbours = ring_buf_init(buffer, NUM_NEIGHBOURS + 1);

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
