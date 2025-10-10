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

// Per-policy resource tracking (Step 2 of assignment)
#define MAX_POLICIES 3  // LRU, PEBS/LFU, SIMPLE
static struct hemem_policy_resources policy_resources[MAX_POLICIES];
static size_t policy_resource_count = 0;

// Fallback policy for unmatched VAs (default: LFU/PEBS)
static enum hemem_policy_kind fallback_policy_kind = HEMEM_POLICY_PEBs;

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
static void pebs_init_wrapper(struct fifo_list *dram_fl, struct fifo_list *nvm_fl)
{
  static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
  static bool initialized = false;
  pthread_mutex_lock(&lock);
  if (!initialized) {
    pebs_init(dram_fl, nvm_fl);
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

static void add_region_locked(uint64_t start, uint64_t end, enum hemem_policy_kind kind, const char *label)
{
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
  regions[region_count].policy_kind = kind;
  regions[region_count].is_fallback = false;
  
  if (label != NULL) {
    snprintf(regions[region_count].label, sizeof(regions[region_count].label), "%s", label);
  } else {
    const char *policy_name = (kind == HEMEM_POLICY_LRU) ? "lru" :
                              (kind == HEMEM_POLICY_PEBs) ? "lfu" : "simple";
    snprintf(regions[region_count].label, sizeof(regions[region_count].label), "%s", policy_name);
  }
  
  region_count++;
  policy_usage[kind]++;
}

// OLD - No longer needed, fallback is implicit
// static void register_default_region_locked(void) { ... }

// New simpler region parser for format: "start-end:policyname,start-end:policyname"
// Example: "0x555555400000-0x7fff1f600000:lru,0x7fff1f800000-0x7fff97800000:lfu"
static void parse_regions_new(const char *spec)
{
  if (spec == NULL || *spec == '\0') {
    return;
  }

  char *spec_copy = strdup(spec);
  char *token = strtok(spec_copy, ",");
  
  while (token != NULL) {
    uint64_t start, end;
    char policy_name[32];
    
    // Parse "0xSTART-0xEND:policyname"
    if (sscanf(token, "%lx-%lx:%31s", &start, &end, policy_name) == 3) {
      enum hemem_policy_kind kind;
      
      if (strcasecmp(policy_name, "lru") == 0) {
        kind = HEMEM_POLICY_LRU;
      } else if (strcasecmp(policy_name, "lfu") == 0 || strcasecmp(policy_name, "pebs") == 0 || strcasecmp(policy_name, "hemem") == 0) {
        kind = HEMEM_POLICY_PEBs;
      } else if (strcasecmp(policy_name, "simple") == 0) {
        kind = HEMEM_POLICY_SIMPLE;
      } else {
        fprintf(stderr, "HeMem: Unknown policy '%s' in region spec, skipping\n", policy_name);
        token = strtok(NULL, ",");
        continue;
      }
      
      add_region_locked(start, end, kind, policy_name);
      fprintf(stderr, "HeMem: Registered region [0x%lx-0x%lx) with policy %s\n", start, end, policy_name);
    } else {
      fprintf(stderr, "HeMem: Failed to parse region spec: '%s'\n", token);
    }
    
    token = strtok(NULL, ",");
  }
  
  free(spec_copy);
}

// OLD parser - keeping for now for backward compat
// OLD parse_region_spec_locked - replaced by parse_regions_new
// (keeping commented out for reference)
#if 0
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

    add_region_locked(start, end, policy->kind, policy->name);
    token = strtok(NULL, ",;");
  }

  free(mutable_spec);
}
#endif

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
  size_t prev_count = region_count;
  add_region_locked(start, end, kind, label);
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

// Helper to parse size strings like "4G", "16G", "512M"
static uint64_t parse_size_string(const char *str)
{
  char *endptr;
  uint64_t value = strtoull(str, &endptr, 10);
  
  if (*endptr == 'G' || *endptr == 'g') {
    value *= (1ULL << 30);
  } else if (*endptr == 'M' || *endptr == 'm') {
    value *= (1ULL << 20);
  } else if (*endptr == 'K' || *endptr == 'k') {
    value *= (1ULL << 10);
  }
  
  return value;
}

// Parse HEMEM_REGION_PHYS="lru:4G:16G,lfu:8G:16G"
// Returns: number of policy resource specs parsed
static int parse_physical_allocation(const char *spec)
{
  if (spec == NULL || *spec == '\0') {
    return 0;
  }
  
  char *spec_copy = strdup(spec);
  char *token = strtok(spec_copy, ",");
  int count = 0;
  
  while (token != NULL && count < MAX_POLICIES) {
    // Parse "policyname:dram_size:nvm_size"
    char policy_name[32];
    char dram_str[32];
    char nvm_str[32];
    
    if (sscanf(token, "%31[^:]:%31[^:]:%31s", policy_name, dram_str, nvm_str) == 3) {
      enum hemem_policy_kind kind;
      
      if (strcasecmp(policy_name, "lru") == 0) {
        kind = HEMEM_POLICY_LRU;
      } else if (strcasecmp(policy_name, "lfu") == 0 || strcasecmp(policy_name, "pebs") == 0) {
        kind = HEMEM_POLICY_PEBs;
      } else if (strcasecmp(policy_name, "simple") == 0) {
        kind = HEMEM_POLICY_SIMPLE;
      } else {
        fprintf(stderr, "HeMem: Unknown policy '%s' in HEMEM_REGION_PHYS\n", policy_name);
        token = strtok(NULL, ",");
        continue;
      }
      
      policy_resources[count].kind = kind;
      policy_resources[count].dram_size = parse_size_string(dram_str);
      policy_resources[count].nvm_size = parse_size_string(nvm_str);
      
      fprintf(stderr, "HeMem: Physical allocation for %s: DRAM=%lu bytes, NVM=%lu bytes\n",
              policy_name, policy_resources[count].dram_size, policy_resources[count].nvm_size);
      
      count++;
    }
    
    token = strtok(NULL, ",");
  }
  
  free(spec_copy);
  policy_resource_count = count;
  return count;
}

// Get or create policy resources entry for a given policy kind
static struct hemem_policy_resources* get_policy_resources(enum hemem_policy_kind kind)
{
  // Check if already exists
  for (size_t i = 0; i < policy_resource_count; i++) {
    if (policy_resources[i].kind == kind) {
      return &policy_resources[i];
    }
  }
  
  // Create new entry if space available
  if (policy_resource_count < MAX_POLICIES) {
    policy_resources[policy_resource_count].kind = kind;
    policy_resources[policy_resource_count].dram_size = 0;
    policy_resources[policy_resource_count].nvm_size = 0;
    return &policy_resources[policy_resource_count++];
  }
  
  return NULL;
}

// Public API for testing: get policy resources by kind
struct hemem_policy_resources* hemem_get_policy_resources(enum hemem_policy_kind kind)
{
  for (size_t i = 0; i < policy_resource_count; i++) {
    if (policy_resources[i].kind == kind) {
      return &policy_resources[i];
    }
  }
  return NULL;
}

void hemem_regions_bootstrap(void)
{
  pthread_mutex_lock(&regions_lock);
  
  if (regions_bootstrapped) {
    pthread_mutex_unlock(&regions_lock);
    return;
  }
  
  fprintf(stderr, "HeMem: ========================================\n");
  fprintf(stderr, "HeMem: Starting bootstrap (Assignment.md implementation)\n");
  fprintf(stderr, "HeMem: ========================================\n");
  
  // ===================================================================
  // STEP 1: CONFIGURATION PARSING
  // ===================================================================
  
  // 1a. Determine fallback policy (default: LFU/PEBS)
  fallback_policy_kind = HEMEM_POLICY_PEBs;  // Default is LFU
  const char *fallback_env = getenv("HEMEM_FALLBACK_POLICY");
  if (fallback_env != NULL) {
    if (strcasecmp(fallback_env, "lru") == 0) {
      fallback_policy_kind = HEMEM_POLICY_LRU;
    } else if (strcasecmp(fallback_env, "lfu") == 0 || strcasecmp(fallback_env, "pebs") == 0) {
      fallback_policy_kind = HEMEM_POLICY_PEBs;
    }
  }
  
  const char *fallback_name = (fallback_policy_kind == HEMEM_POLICY_LRU) ? "LRU" : "LFU";
  fprintf(stderr, "HeMem: Fallback policy: %s\n", fallback_name);
  
  // 1b. Check for backwards compat: HEMEM_POLICY (single policy for entire VA space)
  if (region_count == 0) {
    const char *simple_policy = getenv("HEMEM_POLICY");
    if (simple_policy != NULL && *simple_policy != '\0') {
      enum hemem_policy_kind kind;
      if (strcasecmp(simple_policy, "lru") == 0) {
        kind = HEMEM_POLICY_LRU;
      } else if (strcasecmp(simple_policy, "lfu") == 0 || strcasecmp(simple_policy, "pebs") == 0 || strcasecmp(simple_policy, "hemem") == 0) {
        kind = HEMEM_POLICY_PEBs;
      } else if (strcasecmp(simple_policy, "simple") == 0) {
        kind = HEMEM_POLICY_SIMPLE;
      } else {
        fprintf(stderr, "HeMem: Unknown policy '%s', using fallback\n", simple_policy);
        kind = fallback_policy_kind;
      }
      
      // In backwards compat mode, no explicit regions, just fallback covers everything
      fallback_policy_kind = kind;
      fprintf(stderr, "HeMem: Backwards compat mode (HEMEM_POLICY=%s), no explicit regions\n", simple_policy);
    }
  }
  
  // 1c. Parse HEMEM_REGIONS for VA ranges (if not already registered manually)
  if (region_count == 0) {
    const char *regions_spec = getenv("HEMEM_REGIONS");
    if (regions_spec != NULL && *regions_spec != '\0') {
      parse_regions_new(regions_spec);
    }
  }
  
  fprintf(stderr, "HeMem: Registered %zu explicit regions\n", region_count);
  for (size_t i = 0; i < region_count; i++) {
    const char *pname = (regions[i].policy_kind == HEMEM_POLICY_LRU) ? "LRU" :
                        (regions[i].policy_kind == HEMEM_POLICY_PEBs) ? "LFU" : "SIMPLE";
    fprintf(stderr, "HeMem:   Region %zu [%s]: VA [0x%lx-0x%lx) policy=%s\n",
            i, regions[i].label, regions[i].start, regions[i].end, pname);
  }
  
  // 1d. Parse HEMEM_REGION_PHYS for physical allocations
  const char *phys_spec = getenv("HEMEM_REGIONS_PHYS");
  if (phys_spec != NULL && *phys_spec != '\0') {
    parse_physical_allocation(phys_spec);
  }
  
  // 1e. Calculate leftover resources for fallback
  uint64_t allocated_dram = 0, allocated_nvm = 0;
  for (size_t i = 0; i < policy_resource_count; i++) {
    allocated_dram += policy_resources[i].dram_size;
    allocated_nvm += policy_resources[i].nvm_size;
  }
  
  uint64_t leftover_dram = (dramsize > allocated_dram) ? (dramsize - allocated_dram) : 0;
  uint64_t leftover_nvm = (nvmsize > allocated_nvm) ? (nvmsize - allocated_nvm) : 0;
  
  fprintf(stderr, "HeMem: Total DAX: DRAM=%lu bytes (%.2f GB), NVM=%lu bytes (%.2f GB)\n",
          dramsize, dramsize / (1024.0*1024.0*1024.0),
          nvmsize, nvmsize / (1024.0*1024.0*1024.0));
  fprintf(stderr, "HeMem: Allocated: DRAM=%lu bytes (%.2f GB), NVM=%lu bytes (%.2f GB)\n",
          allocated_dram, allocated_dram / (1024.0*1024.0*1024.0),
          allocated_nvm, allocated_nvm / (1024.0*1024.0*1024.0));
  fprintf(stderr, "HeMem: Leftover for fallback: DRAM=%lu bytes (%.2f GB), NVM=%lu bytes (%.2f GB)\n",
          leftover_dram, leftover_dram / (1024.0*1024.0*1024.0),
          leftover_nvm, leftover_nvm / (1024.0*1024.0*1024.0));
  
  // 1f. Merge leftover into fallback policy's resources
  struct hemem_policy_resources *fallback_res = get_policy_resources(fallback_policy_kind);
  if (fallback_res != NULL) {
    fallback_res->dram_size += leftover_dram;
    fallback_res->nvm_size += leftover_nvm;
    const char *fb_name = (fallback_policy_kind == HEMEM_POLICY_LRU) ? "LRU" : "LFU";
    fprintf(stderr, "HeMem: Fallback policy (%s) total: DRAM=%lu bytes (%.2f GB), NVM=%lu bytes (%.2f GB)\n",
            fb_name,
            fallback_res->dram_size, fallback_res->dram_size / (1024.0*1024.0*1024.0),
            fallback_res->nvm_size, fallback_res->nvm_size / (1024.0*1024.0*1024.0));
  }
  
  // ===================================================================
  // STEP 2: RESOURCE ALLOCATION - Create page lists for each policy
  // ===================================================================
  
  fprintf(stderr, "HeMem: Creating page lists for %zu policies\n", policy_resource_count);
  
  uint64_t dram_offset = 0;
  uint64_t nvm_offset = 0;
  
  for (size_t i = 0; i < policy_resource_count; i++) {
    struct hemem_policy_resources *res = &policy_resources[i];
    
    // Initialize free lists
    pthread_mutex_init(&(res->dram_free_list.list_lock), NULL);
    res->dram_free_list.first = NULL;
    res->dram_free_list.last = NULL;
    res->dram_free_list.numentries = 0;
    
    pthread_mutex_init(&(res->nvm_free_list.list_lock), NULL);
    res->nvm_free_list.first = NULL;
    res->nvm_free_list.last = NULL;
    res->nvm_free_list.numentries = 0;
    
    // Track physical offsets
    res->dram_offset_start = dram_offset;
    res->nvm_offset_start = nvm_offset;
    
    // Initialize per-policy migration statistics
    res->migrations_up = 0;
    res->migrations_down = 0;
    res->bytes_migrated = 0;
    
    // Create DRAM pages (2MB each)
    uint64_t dram_pages = res->dram_size / PAGE_SIZE;
    for (uint64_t j = 0; j < dram_pages; j++) {
      struct hemem_page *p = calloc(1, sizeof(struct hemem_page));
      p->devdax_offset = dram_offset + (j * PAGE_SIZE);
      p->present = false;
      p->in_dram = true;
      p->pt = pagesize_to_pt(PAGE_SIZE);
      pthread_mutex_init(&(p->page_lock), NULL);
      enqueue_fifo(&res->dram_free_list, p);
    }
    
    // Create NVM pages (2MB each)
    uint64_t nvm_pages = res->nvm_size / PAGE_SIZE;
    for (uint64_t j = 0; j < nvm_pages; j++) {
      struct hemem_page *p = calloc(1, sizeof(struct hemem_page));
      p->devdax_offset = nvm_offset + (j * PAGE_SIZE);
      p->present = false;
      p->in_dram = false;
      p->pt = pagesize_to_pt(PAGE_SIZE);
      pthread_mutex_init(&(p->page_lock), NULL);
      enqueue_fifo(&res->nvm_free_list, p);
    }
    
    const char *policy_name = (res->kind == HEMEM_POLICY_LRU) ? "LRU" :
                              (res->kind == HEMEM_POLICY_PEBs) ? "LFU" : "SIMPLE";
    fprintf(stderr, "HeMem: Policy %s: %lu DRAM pages, %lu NVM pages (DRAM offset=0x%lx, NVM offset=0x%lx)\n",
            policy_name, dram_pages, nvm_pages, dram_offset, nvm_offset);
    
    dram_offset += res->dram_size;
    nvm_offset += res->nvm_size;
  }
  
  regions_bootstrapped = true;
  pthread_mutex_unlock(&regions_lock);
  
  HEMEM_PT_LOCK_ACQUIRE();
  hemem_page_table_shutting_down = false;
  HEMEM_PT_LOCK_RELEASE();
  
  ensure_page_table();
  
  // ===================================================================
  // STEP 3: POLICY INITIALIZATION - Pass page lists to policies
  // ===================================================================
  
  // Skip if HEMEM_NO_THREADS is set (for testing)
  if (getenv("HEMEM_NO_THREADS") != NULL) {
    fprintf(stderr, "HeMem: Skipping policy initialization (HEMEM_NO_THREADS set)\n");
    fprintf(stderr, "HeMem: ========================================\n");
    fprintf(stderr, "HeMem: Bootstrap complete\n");
    fprintf(stderr, "HeMem: ========================================\n");
    return;
  }
  
  fprintf(stderr, "HeMem: Initializing %zu unique policies\n", policy_resource_count);
  
  // Initialize each unique policy with its free lists
  for (size_t i = 0; i < policy_resource_count; i++) {
    struct hemem_policy_resources *res = &policy_resources[i];
    const struct hemem_policy_ops *ops = policy_by_kind(res->kind);
    
    if (ops != NULL && ops->init != NULL && !policy_initialized[res->kind]) {
      fprintf(stderr, "HeMem: Calling %s->init() with %lu DRAM pages and %lu NVM pages\n",
              ops->name,
              res->dram_free_list.numentries,
              res->nvm_free_list.numentries);
      
      // Pass the pre-allocated free lists to the policy
      ops->init(&res->dram_free_list, &res->nvm_free_list);
      
      policy_initialized[res->kind] = true;
    }
  }
  
  fprintf(stderr, "HeMem: ========================================\n");
  fprintf(stderr, "HeMem: Bootstrap complete\n");
  fprintf(stderr, "HeMem: ========================================\n");
}

struct hemem_region* hemem_region_lookup(uint64_t va)
{
  pthread_mutex_lock(&regions_lock);
  
  // First, check explicit regions
  for (size_t i = 0; i < region_count; i++) {
    if (va >= regions[i].start && va < regions[i].end) {
      struct hemem_region *match = &regions[i];
      pthread_mutex_unlock(&regions_lock);
      return match;
    }
  }
  
  // No explicit region matched, return implicit fallback
  // Create a synthetic fallback region (not stored in regions array)
  static struct hemem_region fallback_region;
  fallback_region.start = 0;
  fallback_region.end = UINT64_MAX;
  fallback_region.policy_kind = fallback_policy_kind;
  fallback_region.is_fallback = true;
  snprintf(fallback_region.label, sizeof(fallback_region.label), "fallback");
  
  pthread_mutex_unlock(&regions_lock);
  return &fallback_region;
}

struct hemem_page* hemem_policy_pagefault(struct hemem_region *region, uint64_t va)
{
  if (region == NULL) {
    return NULL;
  }
  
  // Get the policy ops for this region
  const struct hemem_policy_ops *policy = policy_by_kind(region->policy_kind);
  if (policy == NULL || policy->pagefault == NULL) {
    return NULL;
  }
  
  struct hemem_page *page = policy->pagefault(va);
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

  // Get the policy ops for this region
  const struct hemem_policy_ops *policy = policy_by_kind(page->region->policy_kind);
  if (policy != NULL && policy->page_add != NULL) {
    policy->page_add(page);
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

  if (page->region != NULL) {
    const struct hemem_policy_ops *policy = policy_by_kind(page->region->policy_kind);
    if (policy != NULL && policy->page_remove != NULL) {
      policy->page_remove(page);
    }
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
  // Print stats for each initialized policy
  for (size_t i = 0; i < sizeof(policy_ops_table) / sizeof(policy_ops_table[0]); i++) {
    enum hemem_policy_kind kind = policy_ops_table[i].kind;
    if (policy_initialized[kind] && policy_ops_table[i].stats != NULL) {
      // Print policy name prefix, then call policy's stats function
      fprintf(stderr, "[%s] ", policy_ops_table[i].name);
      policy_ops_table[i].stats();
      
      // Also print per-policy migration stats if this policy has resources allocated
      for (size_t j = 0; j < policy_resource_count; j++) {
        if (policy_resources[j].kind == kind) {
          fprintf(stderr, "[%s] migrations_up: [%lu]\tmigrations_down: [%lu]\tbytes_migrated: [%lu]\n",
                  policy_ops_table[i].name,
                  policy_resources[j].migrations_up,
                  policy_resources[j].migrations_down,
                  policy_resources[j].bytes_migrated);
          break;
        }
      }
    }
  }
  
  // Special handling for fallback policy if it wasn't explicitly initialized
  // (e.g., only has implicit fallback region with no explicit regions of that type)
  if (!policy_initialized[fallback_policy_kind]) {
    const struct hemem_policy_ops *fallback_ops = policy_by_kind(fallback_policy_kind);
    if (fallback_ops != NULL && fallback_ops->stats != NULL) {
      fprintf(stderr, "[%s-FALLBACK] ", fallback_ops->name);
      // Note: Can't call stats() since policy isn't initialized, just print resource info
      for (size_t j = 0; j < policy_resource_count; j++) {
        if (policy_resources[j].kind == fallback_policy_kind) {
          fprintf(stderr, "migrations_up: [%lu]\tmigrations_down: [%lu]\tbytes_migrated: [%lu]\n",
                  policy_resources[j].migrations_up,
                  policy_resources[j].migrations_down,
                  policy_resources[j].bytes_migrated);
          break;
        }
      }
    }
  }
}

void hemem_policy_record_migration(enum hemem_policy_kind kind, bool to_dram, uint64_t bytes)
{
  // Find the policy_resources entry for this policy and update its migration counters
  for (size_t i = 0; i < policy_resource_count; i++) {
    if (policy_resources[i].kind == kind) {
      if (to_dram) {
        policy_resources[i].migrations_up++;
      } else {
        policy_resources[i].migrations_down++;
      }
      policy_resources[i].bytes_migrated += bytes;
      return;
    }
  }
  // If we get here, policy wasn't found - shouldn't happen but don't crash
  fprintf(stderr, "HeMem: Warning: migration recorded for unknown policy kind %d\n", kind);
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
