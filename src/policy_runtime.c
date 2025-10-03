#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
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
#define HEMEM_ENV_GLOBAL_POLICY "GLOBAL_HEMEM_POLICY"
#define HEMEM_ENV_LEGACY_POLICY "HEMEM_POLICY"
#define HEMEM_ENV_VA_MAP "HEMEM_REGION_VA_MAP"
#define HEMEM_ENV_PHYS_MAP "HEMEM_REGION_PHYS_MAP"

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

struct region_phys_assignment {
  int id;
  uint64_t dram_size;
  uint64_t nvm_size;
};

static const struct hemem_policy_ops* policy_by_name(const char *name);
static char *trim(char *s);
static uint64_t parse_u64(const char *s);
static void add_region_locked(uint64_t start, uint64_t end, const struct hemem_policy_ops *policy, const char *label, int region_id);

static uint64_t parse_size_with_suffix(const char *input)
{
  if (input == NULL || *input == '\0') {
    return 0;
  }

  char *endptr = NULL;
  errno = 0;
  unsigned long long base = strtoull(input, &endptr, 0);
  if (errno != 0) {
    fprintf(stderr, "HeMem: failed to parse size '%s' (%s)\n", input, strerror(errno));
    return 0;
  }

  uint64_t multiplier = 1;
  if (endptr != NULL) {
    if (strcasecmp(endptr, "G") == 0) {
      multiplier = 1ULL << 30;
    } else if (strcasecmp(endptr, "M") == 0) {
      multiplier = 1ULL << 20;
    } else if (strcasecmp(endptr, "K") == 0) {
      multiplier = 1ULL << 10;
    } else if (*endptr == '\0' || strcasecmp(endptr, "B") == 0) {
      multiplier = 1ULL;
    } else {
      fprintf(stderr, "HeMem: unknown size suffix '%s' in '%s'\n", endptr, input);
      return 0;
    }
  }

  return base * multiplier;
}

static int parse_region_id(const char *token)
{
  if (token == NULL || *token == '\0') {
    return -1;
  }
  errno = 0;
  long value = strtol(token, NULL, 10);
  if (errno != 0 || value < 0 || value > INT32_MAX) {
    fprintf(stderr, "HeMem: invalid region id '%s'\n", token);
    return -1;
  }
  return (int)value;
}

static size_t parse_phys_partitions(const char *spec, struct region_phys_assignment *assignments, size_t max_assignments)
{
  if (spec == NULL || *spec == '\0') {
    return 0;
  }

  size_t count = 0;
  char *mutable_spec = strdup(spec);
  if (mutable_spec == NULL) {
    fprintf(stderr, "HeMem: failed to duplicate physical partition spec\n");
    return 0;
  }

  char *cursor = mutable_spec;
  while (cursor != NULL && *cursor != '\0' && count < max_assignments) {
    char *next = strstr(cursor, "::");
    if (next != NULL) {
      *next = '\0';
    }

    char *entry = trim(cursor);
    if (*entry != '\0') {
      char *component_buffer = strdup(entry);
      if (component_buffer == NULL) {
        fprintf(stderr, "HeMem: failed to duplicate region partition token\n");
        break;
      }

      char *saveptr_field = NULL;
      char *id_token = strtok_r(component_buffer, ":", &saveptr_field);
      char *dram_token = strtok_r(NULL, ":", &saveptr_field);
      char *nvm_token = strtok_r(NULL, ":", &saveptr_field);

      if (id_token != NULL) id_token = trim(id_token);
      if (dram_token != NULL) dram_token = trim(dram_token);
      if (nvm_token != NULL) nvm_token = trim(nvm_token);

      if (id_token == NULL || dram_token == NULL || nvm_token == NULL || *id_token == '\0' || *dram_token == '\0' || *nvm_token == '\0') {
        fprintf(stderr, "HeMem: invalid physical partition entry '%s'\n", entry);
        free(component_buffer);
      } else {
        int region_id = parse_region_id(id_token);
        if (region_id >= 0) {
          uint64_t dram_size = parse_size_with_suffix(dram_token);
          uint64_t nvm_size = parse_size_with_suffix(nvm_token);

          assignments[count].id = region_id;
          assignments[count].dram_size = dram_size;
          assignments[count].nvm_size = nvm_size;
          count++;
        }
        free(component_buffer);
      }
    }

    if (next == NULL) {
      break;
    }
    cursor = next + 2;
  }

  free(mutable_spec);
  return count;
}

static size_t parse_va_regions(const char *spec)
{
  if (spec == NULL || *spec == '\0') {
    return 0;
  }

  size_t added = 0;
  char *mutable_spec = strdup(spec);
  if (mutable_spec == NULL) {
    fprintf(stderr, "HeMem: failed to duplicate VA region spec\n");
    return 0;
  }

  char *cursor = mutable_spec;
  while (cursor != NULL && *cursor != '\0') {
    char *next = strstr(cursor, "::");
    if (next != NULL) {
      *next = '\0';
    }

    char *entry = trim(cursor);
    if (*entry != '\0') {
      char *component_buffer = strdup(entry);
      if (component_buffer == NULL) {
        fprintf(stderr, "HeMem: failed to duplicate VA region token\n");
        break;
      }

      char *saveptr_field = NULL;
      char *id_token = strtok_r(component_buffer, ":", &saveptr_field);
      char *policy_token = strtok_r(NULL, ":", &saveptr_field);
      char *start_token = strtok_r(NULL, ":", &saveptr_field);
      char *end_token = strtok_r(NULL, ":", &saveptr_field);

      if (id_token != NULL) id_token = trim(id_token);
      if (policy_token != NULL) policy_token = trim(policy_token);
      if (start_token != NULL) start_token = trim(start_token);
      if (end_token != NULL) end_token = trim(end_token);

      if (id_token == NULL || policy_token == NULL || start_token == NULL || end_token == NULL ||
          *id_token == '\0' || *policy_token == '\0' || *start_token == '\0' || *end_token == '\0') {
        fprintf(stderr, "HeMem: invalid VA region entry '%s'\n", entry);
        free(component_buffer);
      } else {
        int region_id = parse_region_id(id_token);
        if (region_id >= 0) {
          const struct hemem_policy_ops *policy = policy_by_name(policy_token);
          if (policy == NULL) {
            fprintf(stderr, "HeMem: unknown policy '%s' in VA map\n", policy_token);
          } else {
            uint64_t start = parse_u64(start_token);
            uint64_t end = parse_u64(end_token);
            if (end <= start) {
              fprintf(stderr, "HeMem: invalid VA range [%s-%s]\n", start_token, end_token);
            } else {
              add_region_locked(start, end, policy, policy->name, region_id);
              added++;
            }
          }
        }
        free(component_buffer);
      }
    }

    if (next == NULL) {
      break;
    }
    cursor = next + 2;
  }

  free(mutable_spec);
  return added;
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
    .name = "hemen",
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

static void add_region_locked(uint64_t start, uint64_t end, const struct hemem_policy_ops *policy, const char *label, int region_id)
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

  int resolved_id = region_id;
  if (region_count >= MAX_HEMEM_REGIONS) {
    fprintf(stderr, "HeMem: region table full, cannot register [%lx-%lx)\n", start, end);
    return;
  }

  if (resolved_id < 0) {
    resolved_id = (int)region_count;
    bool unique = false;
    while (!unique) {
      unique = true;
      for (size_t i = 0; i < region_count; i++) {
        if (regions[i].id == resolved_id) {
          resolved_id++;
          unique = false;
          break;
        }
      }
    }
  } else {
    for (size_t i = 0; i < region_count; i++) {
      if (regions[i].id == resolved_id) {
        fprintf(stderr, "HeMem: duplicate region id %d ignored\n", resolved_id);
        return;
      }
    }
  }

  for (size_t i = 0; i < region_count; i++) {
    if (!(end <= regions[i].start || start >= regions[i].end)) {
      fprintf(stderr, "HeMem: VA region [%lx-%lx) overlaps with existing region %d [%lx-%lx)\n",
              start, end, regions[i].id, regions[i].start, regions[i].end);
      return;
    }
  }

  memset(&regions[region_count], 0, sizeof(regions[region_count]));
  regions[region_count].start = start;
  regions[region_count].end = end;
  regions[region_count].policy = policy;
  regions[region_count].id = resolved_id;
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
  add_region_locked(0, UINT64_MAX, policy, "default", -1);
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

  add_region_locked(start, end, policy, policy->name, -1);
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
  add_region_locked(start, end, policy, label, -1);
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
    const char *global_policy_env = getenv(HEMEM_ENV_GLOBAL_POLICY);
    if (global_policy_env == NULL || *global_policy_env == '\0') {
      global_policy_env = getenv(HEMEM_ENV_LEGACY_POLICY);
    }

    if (global_policy_env != NULL && *global_policy_env != '\0') {
      const struct hemem_policy_ops *policy = policy_by_name(global_policy_env);
      if (policy != NULL) {
        add_region_locked(0, UINT64_MAX, policy, global_policy_env, 0);
        fprintf(stderr, "HeMem: Using single policy '%s' for entire VA/physical space (env override)\n", global_policy_env);
      } else {
        fprintf(stderr, "HeMem: Unknown policy '%s' in %s, falling back to defaults\n", global_policy_env, HEMEM_ENV_GLOBAL_POLICY);
      }
    }

    if (region_count == 0) {
      const char *va_spec = getenv(HEMEM_ENV_VA_MAP);
      if (va_spec != NULL && *va_spec != '\0') {
        parse_va_regions(va_spec);
      }
    }

    if (region_count == 0) {
      const char *legacy_spec = getenv("HEMEM_REGIONS");
      if (legacy_spec != NULL && *legacy_spec != '\0') {
        parse_region_spec_locked(legacy_spec);
      }
    }

    if (region_count == 0) {
      register_default_region_locked();
    }

    sort_regions_locked();

  struct region_phys_assignment assignments[MAX_HEMEM_REGIONS] = {0};
    size_t assignment_count = 0;
    const char *phys_spec = getenv(HEMEM_ENV_PHYS_MAP);
    if (phys_spec != NULL && *phys_spec != '\0') {
      assignment_count = parse_phys_partitions(phys_spec, assignments, MAX_HEMEM_REGIONS);
    }

    for (size_t i = 0; i < region_count; i++) {
      regions[i].dram_offset_start = 0;
      regions[i].dram_size = 0;
      regions[i].nvm_offset_start = 0;
      regions[i].nvm_size = 0;
    }

    uint64_t total_dram = dramsize;
    uint64_t total_nvm = nvmsize;
    uint64_t dram_consumed = 0;
    uint64_t nvm_consumed = 0;

    for (size_t i = 0; i < assignment_count; i++) {
      bool matched = false;
      for (size_t j = 0; j < region_count; j++) {
        if (regions[j].id == assignments[i].id) {
          matched = true;
          uint64_t dram_size = assignments[i].dram_size;
          uint64_t nvm_size = assignments[i].nvm_size;

          dram_size = (dram_size / PAGE_SIZE) * PAGE_SIZE;
          nvm_size = (nvm_size / PAGE_SIZE) * PAGE_SIZE;

          if (dram_consumed + dram_size > total_dram) {
            fprintf(stderr, "HeMem: DRAM assignment for region %d truncated to available memory\n", regions[j].id);
            dram_size = (total_dram > dram_consumed) ? (total_dram - dram_consumed) : 0;
          }
          if (nvm_consumed + nvm_size > total_nvm) {
            fprintf(stderr, "HeMem: NVM assignment for region %d truncated to available memory\n", regions[j].id);
            nvm_size = (total_nvm > nvm_consumed) ? (total_nvm - nvm_consumed) : 0;
          }

          regions[j].dram_offset_start = dram_consumed;
          regions[j].dram_size = dram_size;
          regions[j].nvm_offset_start = nvm_consumed;
          regions[j].nvm_size = nvm_size;

          dram_consumed += dram_size;
          nvm_consumed += nvm_size;
          break;
        }
      }
      if (!matched) {
        fprintf(stderr, "HeMem: physical partition references unknown region id %d\n", assignments[i].id);
      }
    }

    size_t remaining_regions = 0;
    for (size_t i = 0; i < region_count; i++) {
      if (regions[i].dram_size == 0 && regions[i].nvm_size == 0) {
        remaining_regions++;
      }
    }

    uint64_t remaining_dram = (dram_consumed < total_dram) ? (total_dram - dram_consumed) : 0;
    uint64_t remaining_nvm = (nvm_consumed < total_nvm) ? (total_nvm - nvm_consumed) : 0;

    for (size_t i = 0; i < region_count; i++) {
      if (regions[i].dram_size == 0 && regions[i].nvm_size == 0) {
        if (remaining_regions > 0) {
          uint64_t dram_share = remaining_dram / remaining_regions;
          uint64_t nvm_share = remaining_nvm / remaining_regions;

          dram_share = (dram_share / PAGE_SIZE) * PAGE_SIZE;
          nvm_share = (nvm_share / PAGE_SIZE) * PAGE_SIZE;

          regions[i].dram_offset_start = dram_consumed;
          regions[i].dram_size = dram_share;
          regions[i].nvm_offset_start = nvm_consumed;
          regions[i].nvm_size = nvm_share;

          dram_consumed += dram_share;
          nvm_consumed += nvm_share;
          if (remaining_dram >= dram_share) {
            remaining_dram -= dram_share;
          } else {
            remaining_dram = 0;
          }
          if (remaining_nvm >= nvm_share) {
            remaining_nvm -= nvm_share;
          } else {
            remaining_nvm = 0;
          }
        }
        if (remaining_regions > 0) {
          remaining_regions--;
        }
      }

      fprintf(stderr, "HeMem: Region id=%d label=%s VA:[0x%lx-0x%lx) -> DRAM:[0x%lx-0x%lx) NVM:[0x%lx-0x%lx)\n",
              regions[i].id, regions[i].label,
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

  // Initialize each policy with its assigned physical memory ranges
  for (size_t i = 0; i < sizeof(policy_ops_table) / sizeof(policy_ops_table[0]); i++) {
    enum hemem_policy_kind kind = policy_ops_table[i].kind;
    if (policy_usage[kind] > 0 && !policy_initialized[kind]) {
      if (policy_ops_table[i].init != NULL) {
        // Find the first region using this policy to get memory ranges
        // For now, we'll use the aggregate of all regions using this policy
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
