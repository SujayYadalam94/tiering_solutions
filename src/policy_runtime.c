#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "hemem.h"
#include "pebs.h"
#include "policies/lru.h"
#include "policies/simple.h"
#include "policies/paging.h"
#include "khash.h"

#ifdef ALLOC_RUNTIME

#define MAX_HEMEM_REGIONS 16

KHASH_MAP_INIT_INT64(hemem_va_map, struct hemem_page*)

static khash_t(hemem_va_map) *hemem_page_table;
static pthread_mutex_t hemem_page_table_lock = PTHREAD_MUTEX_INITIALIZER;
static bool hemem_page_table_shutting_down = false;
/*
#define HEMEM_PT_LOCK_ACQUIRE() \
  do { \
    fprintf(stderr, "[HEMEM-LOCK] %s:%d attempting lock\n", __func__, __LINE__); \
    int __hemem_lock_rc = pthread_mutex_lock(&hemem_page_table_lock); \
    if (__hemem_lock_rc != 0) { \
      fprintf(stderr, "[HEMEM-LOCK] %s:%d lock failed: %s\n", __func__, __LINE__, strerror(__hemem_lock_rc)); \
    } else { \
      fprintf(stderr, "[HEMEM-LOCK] %s:%d lock acquired\n", __func__, __LINE__); \
    } \
  } while (0)

#define HEMEM_PT_LOCK_RELEASE() \
  do { \
    fprintf(stderr, "[HEMEM-LOCK] %s:%d releasing lock\n", __func__, __LINE__); \
    int __hemem_unlock_rc = pthread_mutex_unlock(&hemem_page_table_lock); \
    if (__hemem_unlock_rc != 0) { \
      fprintf(stderr, "[HEMEM-LOCK] %s:%d unlock failed: %s\n", __func__, __LINE__, strerror(__hemem_unlock_rc)); \
    } else { \
      fprintf(stderr, "[HEMEM-LOCK] %s:%d lock released\n", __func__, __LINE__); \
    } \
  } while (0)
*/

#define HEMEM_PT_LOCK_ACQUIRE() \
  do { \
    int __hemem_lock_rc = pthread_mutex_lock(&hemem_page_table_lock); \
    if (__hemem_lock_rc != 0) { \
    } else { \
    } \
  } while (0)

#define HEMEM_PT_LOCK_RELEASE() \
  do { \
    int __hemem_unlock_rc = pthread_mutex_unlock(&hemem_page_table_lock); \
    if (__hemem_unlock_rc != 0) { \
    } else { \
    } \
  } while (0)

static struct hemem_region regions[MAX_HEMEM_REGIONS];
static size_t region_count;
static bool regions_bootstrapped;
static pthread_mutex_t regions_lock = PTHREAD_MUTEX_INITIALIZER;

static uint32_t policy_usage[HEMEM_POLICY_COUNT];
static bool policy_initialized[HEMEM_POLICY_COUNT];

struct hemem_policy_ops;

// Test helper to reset region state
void hemem_regions_reset(void) {
  pthread_mutex_lock(&regions_lock);
  memset(regions, 0, sizeof(regions));
  region_count = 0;
  regions_bootstrapped = false;
  memset(policy_usage, 0, sizeof(policy_usage));
  memset(policy_initialized, 0, sizeof(policy_initialized));
  pthread_mutex_unlock(&regions_lock);
}

static void ensure_page_table(void)
{
  if (hemem_page_table == NULL && !hemem_page_table_shutting_down) {
    hemem_page_table = kh_init(hemem_va_map);
  }
}

// Wrapper functions that match the old signature but will be called with ranges
static void pebs_init_wrapper(uint64_t dram_offset, uint64_t dram_size, uint64_t nvm_offset, uint64_t nvm_size)
{
  static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
  static bool initialized = false;
  pthread_mutex_lock(&lock);
  if (!initialized) {
    pebs_init(dram_offset, dram_size, nvm_offset, nvm_size);
    initialized = true;
  }
  pthread_mutex_unlock(&lock);
}

static void pebs_shutdown_once(void)
{
  static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
  static bool shutdown_done = false;
  pthread_mutex_lock(&lock);
  if (!shutdown_done) {
    pebs_shutdown();
    shutdown_done = true;
  }
  pthread_mutex_unlock(&lock);
}

static struct hemem_page* pebs_pagefault_wrapper(uint64_t va)
{
  (void)va;
  return pebs_pagefault();
}

static void pebs_add_wrapper(struct hemem_page *page)
{
  pebs_add_page(page);
}

static void pebs_remove_wrapper(struct hemem_page *page)
{
  pebs_remove_page(page);
}

static struct hemem_page* lru_pagefault_wrapper(uint64_t va)
{
  (void)va;
  return lru_pagefault();
}

static void lru_add_wrapper(struct hemem_page *page)
{
  (void)page;
  // LRU policy enqueues pages as part of pagefault handling.
}

static void simple_add_wrapper(struct hemem_page *page)
{
  (void)page;
  // Simple policy maintains state within pagefault handler.
}

static struct hemem_page* simple_pagefault_wrapper(uint64_t va)
{
  (void)va;
  return simple_pagefault();
}

static const struct hemem_policy_ops policy_ops_table[] = {
  {
    .kind = HEMEM_POLICY_PEBs,
    .name = "hemem",
    .init = pebs_init_wrapper,
    .shutdown = pebs_shutdown_once,
    .pagefault = pebs_pagefault_wrapper,
    .page_add = pebs_add_wrapper,
    .page_remove = pebs_remove_wrapper,
    .stats = pebs_stats,
  },
  {
    .kind = HEMEM_POLICY_LRU,
    .name = "lru",
    .init = lru_init,
    .shutdown = lru_shutdown,
    .pagefault = lru_pagefault_wrapper,
    .page_add = lru_add_wrapper,
    .page_remove = lru_remove_page,
    .stats = lru_stats,
  },
  {
    .kind = HEMEM_POLICY_SIMPLE,
    .name = "simple",
    .init = simple_init,
    .shutdown = NULL,
    .pagefault = simple_pagefault_wrapper,
    .page_add = simple_add_wrapper,
    .page_remove = simple_remove_page,
    .stats = simple_stats,
  },
};

static const struct hemem_policy_ops* policy_by_kind(enum hemem_policy_kind kind)
{
  for (size_t i = 0; i < sizeof(policy_ops_table) / sizeof(policy_ops_table[0]); i++) {
    if (policy_ops_table[i].kind == kind) {
      return &policy_ops_table[i];
    }
  }
  return NULL;
}

static const struct hemem_policy_ops* policy_by_name(const char *name)
{
  for (size_t i = 0; i < sizeof(policy_ops_table) / sizeof(policy_ops_table[0]); i++) {
    if (strcasecmp(policy_ops_table[i].name, name) == 0) {
      return &policy_ops_table[i];
    }
  }
  if (strcasecmp("pebs", name) == 0) {
    return policy_by_kind(HEMEM_POLICY_PEBs);
  }
  return NULL;
}

static char *trim(char *s)
{
  while (isspace((unsigned char)*s)) {
    s++;
  }
  if (*s == '\0') {
    return s;
  }
  char *end = s + strlen(s) - 1;
  while (end > s && isspace((unsigned char)*end)) {
    *end-- = '\0';
  }
  return s;
}

static uint64_t parse_u64(const char *s)
{
  errno = 0;
  uint64_t value = strtoull(s, NULL, 0);
  if (errno != 0) {
    fprintf(stderr, "HeMem: failed to parse integer '%s' (%s)\n", s, strerror(errno));
    return 0;
  }
  return value;
}

static void add_region_locked(uint64_t start, uint64_t end, const struct hemem_policy_ops *policy, const char *label)
{
  if (policy == NULL) {
    return;
  }

  if (end <= start) {
    fprintf(stderr, "HeMem: invalid region bounds [%lx, %lx)\n", start, end);
    return;
  }

  bool end_is_max = (end == UINT64_MAX);
  start &= ~(PAGE_SIZE - 1ULL);
  if (!end_is_max) {
    end = (end + PAGE_SIZE - 1ULL) & ~(PAGE_SIZE - 1ULL);
  } else {
    end = UINT64_MAX;
  }

  if (end <= start) {
    fprintf(stderr, "HeMem: region bounds collapse after alignment [%lx, %lx)\n", start, end);
    return;
  }

  if (region_count >= MAX_HEMEM_REGIONS) {
    fprintf(stderr, "HeMem: region table full, cannot register [%lx-%lx)\n", start, end);
    return;
  }

  regions[region_count].start = start;
  regions[region_count].end = end;
  regions[region_count].policy = policy;
  if (label != NULL) {
    snprintf(regions[region_count].label, sizeof(regions[region_count].label), "%s", label);
  } else {
    snprintf(regions[region_count].label, sizeof(regions[region_count].label), "%s", policy->name);
  }
  region_count++;
  policy_usage[policy->kind]++;
}

static void register_default_region_locked(void)
{
  for (size_t i = 0; i < region_count; i++) {
    if (regions[i].start == 0 && regions[i].end == UINT64_MAX) {
      return;
    }
  }
  const struct hemem_policy_ops *policy = policy_by_kind(HEMEM_POLICY_PEBs);
  add_region_locked(0, UINT64_MAX, policy, "default");
}

static void parse_region_spec_locked(const char *spec)
{
  if (spec == NULL || *spec == '\0') {
    return;
  }

  char *mutable_spec = strdup(spec);
  if (mutable_spec == NULL) {
    fprintf(stderr, "HeMem: failed to duplicate region specification string\n");
    return;
  }

  char *token = strtok(mutable_spec, ",;");
  while (token != NULL) {
    char *entry = trim(token);
    if (*entry == '\0') {
      token = strtok(NULL, ",;");
      continue;
    }

    char *colon = strchr(entry, ':');
    if (colon == NULL) {
      fprintf(stderr, "HeMem: invalid region spec '%s' (missing policy)\n", entry);
      token = strtok(NULL, ",;");
      continue;
    }

    *colon = '\0';
    char *range_str = trim(entry);
    char *policy_str = trim(colon + 1);

    char *dash = strchr(range_str, '-');
    if (dash == NULL) {
      fprintf(stderr, "HeMem: invalid region spec '%s' (missing '-')\n", range_str);
      token = strtok(NULL, ",;");
      continue;
    }
    *dash = '\0';

    char *start_str = trim(range_str);
    char *end_str = trim(dash + 1);

    uint64_t start = parse_u64(start_str);
    uint64_t end = parse_u64(end_str);
    if (end <= start) {
      fprintf(stderr, "HeMem: invalid region range '%s-%s'\n", start_str, end_str);
      token = strtok(NULL, ",;");
      continue;
    }

    const struct hemem_policy_ops *policy = policy_by_name(policy_str);
    if (policy == NULL) {
      fprintf(stderr, "HeMem: unknown policy '%s'\n", policy_str);
      token = strtok(NULL, ",;");
      continue;
    }

    add_region_locked(start, end, policy, policy->name);
    token = strtok(NULL, ",;");
  }

  free(mutable_spec);
}

static void sort_regions_locked(void)
{
  if (region_count < 2) {
    return;
  }
  for (size_t i = 0; i < region_count - 1; i++) {
    for (size_t j = i + 1; j < region_count; j++) {
      if (regions[j].start < regions[i].start) {
        struct hemem_region tmp = regions[i];
        regions[i] = regions[j];
        regions[j] = tmp;
      }
    }
  }
}

int hemem_region_register(uint64_t start, uint64_t end, enum hemem_policy_kind kind, const char *label)
{
  pthread_mutex_lock(&regions_lock);
  bool bootstrapped = regions_bootstrapped;
  const struct hemem_policy_ops *policy = policy_by_kind(kind);
  if (policy == NULL) {
    pthread_mutex_unlock(&regions_lock);
    return -EINVAL;
  }
  size_t prev_count = region_count;
  add_region_locked(start, end, policy, label);
  if (region_count == prev_count) {
    pthread_mutex_unlock(&regions_lock);
    return -ENOSPC;
  }
  sort_regions_locked();
  pthread_mutex_unlock(&regions_lock);

  if (bootstrapped) {
    hemem_regions_bootstrap();
  }
  return 0;
}

void hemem_regions_bootstrap(void)
{
  pthread_mutex_lock(&regions_lock);
  if (!regions_bootstrapped) {
    // If no regions have been registered yet, check environment variables
    if (region_count == 0) {
      // Check for simple single-policy configuration first
      const char *simple_policy = getenv("HEMEM_POLICY");
      if (simple_policy != NULL && *simple_policy != '\0') {
        // User specified a single policy for entire VA space
        const struct hemem_policy_ops *policy = policy_by_name(simple_policy);
        if (policy != NULL) {
          add_region_locked(0, UINT64_MAX, policy, simple_policy);
          LOG("HeMem: Using single policy '%s' for entire VA space (via HEMEM_POLICY)\n", simple_policy);
        } else {
          fprintf(stderr, "HeMem: Unknown policy '%s' in HEMEM_POLICY, falling back to default\n", simple_policy);
          register_default_region_locked();
        }
      } else {
        // Use multi-region configuration via HEMEM_REGIONS
        const char *spec = getenv("HEMEM_REGIONS");
        parse_region_spec_locked(spec);
        register_default_region_locked();
      }
    }
    // Otherwise, use the manually registered regions
    
    sort_regions_locked();
    
    // Partition physical memory proportionally to virtual address space
    // (or equally if total_va is 0 or very large)
    uint64_t total_dram = dramsize;
    uint64_t total_nvm = nvmsize;
    uint64_t dram_offset = 0;
    uint64_t nvm_offset = 0;
    
    for (size_t i = 0; i < region_count; i++) {
      if (regions[i].policy == NULL) continue;
      
      uint64_t region_va_size = regions[i].end - regions[i].start;
      uint64_t region_dram_size, region_nvm_size;
      
      // If this is a catchall region (0 to UINT64_MAX), give it all remaining memory
      if (regions[i].start == 0 && regions[i].end == UINT64_MAX) {
        region_dram_size = total_dram - dram_offset;
        region_nvm_size = total_nvm - nvm_offset;
      }
      // Otherwise, equal split among regions (simple and predictable)
      else {
        uint64_t regions_remaining = region_count - i;
        region_dram_size = (total_dram - dram_offset) / regions_remaining;
        region_nvm_size = (total_nvm - nvm_offset) / regions_remaining;
      }
      
      // Align to page boundaries
      region_dram_size = (region_dram_size / PAGE_SIZE) * PAGE_SIZE;
      region_nvm_size = (region_nvm_size / PAGE_SIZE) * PAGE_SIZE;
      
      regions[i].dram_offset_start = dram_offset;
      regions[i].dram_size = region_dram_size;
      regions[i].nvm_offset_start = nvm_offset;
      regions[i].nvm_size = region_nvm_size;
      
      dram_offset += region_dram_size;
      nvm_offset += region_nvm_size;
      
      fprintf(stderr, "HeMem: Region %zu [%s] VA:[0x%lx-0x%lx) -> DRAM:[0x%lx-0x%lx) NVM:[0x%lx-0x%lx)\n",
              i, regions[i].label,
              regions[i].start, regions[i].end,
              regions[i].dram_offset_start, regions[i].dram_offset_start + regions[i].dram_size,
              regions[i].nvm_offset_start, regions[i].nvm_offset_start + regions[i].nvm_size);
    }

    
    regions_bootstrapped = true;
  }
  pthread_mutex_unlock(&regions_lock);

  HEMEM_PT_LOCK_ACQUIRE();
  hemem_page_table_shutting_down = false;
  HEMEM_PT_LOCK_RELEASE();

  ensure_page_table();

  // Skip policy initialization if HEMEM_NO_THREADS is set (for testing)
  if (getenv("HEMEM_NO_THREADS") != NULL) {
    fprintf(stderr, "HeMem: Skipping policy thread initialization (HEMEM_NO_THREADS set)\n");
    return;
  }

  // Initialize each policy with its assigned physical memory ranges
  for (size_t i = 0; i < sizeof(policy_ops_table) / sizeof(policy_ops_table[0]); i++) {
    enum hemem_policy_kind kind = policy_ops_table[i].kind;
    if (policy_usage[kind] > 0 && !policy_initialized[kind]) {
      if (policy_ops_table[i].init != NULL) {
        // Find the aggregate memory ranges for this policy across all its regions
        uint64_t dram_start = UINT64_MAX;
        uint64_t dram_end = 0;
        uint64_t nvm_start = UINT64_MAX;
        uint64_t nvm_end = 0;
        
        pthread_mutex_lock(&regions_lock);
        for (size_t j = 0; j < region_count; j++) {
          if (regions[j].policy != NULL && regions[j].policy->kind == kind) {
            if (regions[j].dram_offset_start < dram_start) {
              dram_start = regions[j].dram_offset_start;
            }
            if (regions[j].dram_offset_start + regions[j].dram_size > dram_end) {
              dram_end = regions[j].dram_offset_start + regions[j].dram_size;
            }
            if (regions[j].nvm_offset_start < nvm_start) {
              nvm_start = regions[j].nvm_offset_start;
            }
            if (regions[j].nvm_offset_start + regions[j].nvm_size > nvm_end) {
              nvm_end = regions[j].nvm_offset_start + regions[j].nvm_size;
            }
          }
        }
        pthread_mutex_unlock(&regions_lock);
        
        uint64_t dram_size = (dram_end > dram_start) ? (dram_end - dram_start) : 0;
        uint64_t nvm_size = (nvm_end > nvm_start) ? (nvm_end - nvm_start) : 0;
        
        if (dram_size > 0 || nvm_size > 0) {
          policy_ops_table[i].init(dram_start, dram_size, nvm_start, nvm_size);
        }
      }
      policy_initialized[kind] = true;
    }
  }
}

struct hemem_region* hemem_region_lookup(uint64_t va)
{
  struct hemem_region *fallback = NULL;
  pthread_mutex_lock(&regions_lock);
  for (size_t i = 0; i < region_count; i++) {
    if (va >= regions[i].start && va < regions[i].end) {
      struct hemem_region *match = &regions[i];
      pthread_mutex_unlock(&regions_lock);
      return match;
    }
    if (regions[i].start == 0 && regions[i].end == UINT64_MAX) {
      fallback = &regions[i];
    }
  }
  pthread_mutex_unlock(&regions_lock);
  return fallback;
}

struct hemem_page* hemem_policy_pagefault(struct hemem_region *region, uint64_t va)
{
  if (region == NULL || region->policy == NULL || region->policy->pagefault == NULL) {
    return NULL;
  }
  struct hemem_page *page = region->policy->pagefault(va);
  if (page != NULL) {
    page->region = region;
  }
  return page;
}

void hemem_policy_register_page(struct hemem_page *page)
{
  if (page == NULL || page->region == NULL) {
    return;
  }

  if (page->region->policy->page_add != NULL) {
    page->region->policy->page_add(page);
  }

  HEMEM_PT_LOCK_ACQUIRE();
  if (hemem_page_table_shutting_down) {
    HEMEM_PT_LOCK_RELEASE();
    return;
  }
  ensure_page_table();
  int status;
  khiter_t key = kh_put(hemem_va_map, hemem_page_table, page->va, &status);
  kh_value(hemem_page_table, key) = page;
  HEMEM_PT_LOCK_RELEASE();
}

void hemem_policy_unregister_page(struct hemem_page *page)
{
  if (page == NULL) {
    return;
  }

  if (page->region != NULL && page->region->policy->page_remove != NULL) {
    page->region->policy->page_remove(page);
  }

  HEMEM_PT_LOCK_ACQUIRE();
  if (hemem_page_table != NULL) {
    khiter_t key = kh_get(hemem_va_map, hemem_page_table, page->va);
    if (key != kh_end(hemem_page_table)) {
      kh_del(hemem_va_map, hemem_page_table, key);
    }
  }
  HEMEM_PT_LOCK_RELEASE();

  page->region = NULL;
}

struct hemem_page* hemem_page_lookup(uint64_t va)
{
  HEMEM_PT_LOCK_ACQUIRE();
  struct hemem_page *page = NULL;
  if (hemem_page_table != NULL) {
    uint64_t aligned = va & ~(PAGE_SIZE - 1ULL);
    khiter_t key = kh_get(hemem_va_map, hemem_page_table, aligned);
    if (key != kh_end(hemem_page_table)) {
      page = kh_value(hemem_page_table, key);
    }
  }
  HEMEM_PT_LOCK_RELEASE();
  return page;
}

void hemem_policies_collect_stats(void)
{
  for (size_t i = 0; i < sizeof(policy_ops_table) / sizeof(policy_ops_table[0]); i++) {
    enum hemem_policy_kind kind = policy_ops_table[i].kind;
    if (policy_usage[kind] > 0 && policy_ops_table[i].stats != NULL) {
      policy_ops_table[i].stats();
    }
  }
}

void hemem_policies_shutdown(void)
{
	fprintf(stderr, "HEMEM: policy shutdown start\n");
  for (size_t i = 0; i < sizeof(policy_ops_table) / sizeof(policy_ops_table[0]); i++) {
    enum hemem_policy_kind kind = policy_ops_table[i].kind;
    if (policy_initialized[kind] && policy_ops_table[i].shutdown != NULL) {
      policy_ops_table[i].shutdown();
      policy_initialized[kind] = false;
    }
  }
	fprintf(stderr, "HEMEM: policy shutdown mid\n");

  khash_t(hemem_va_map) *old_table = NULL;
  HEMEM_PT_LOCK_ACQUIRE();
  hemem_page_table_shutting_down = true;
  if (hemem_page_table != NULL) {
    old_table = hemem_page_table;
    hemem_page_table = NULL;
  }
  HEMEM_PT_LOCK_RELEASE();

  if (old_table != NULL) {
    kh_destroy(hemem_va_map, old_table);
  }
	fprintf(stderr, "HEMEM: policy shutdown end\n");
}

#endif /* ALLOC_RUNTIME */
