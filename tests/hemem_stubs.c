/*
 * Stub implementations for HeMem functions
 * Used for unit testing without requiring kernel userfaultfd extensions
 */

#include <stdint.h>
#include <stdbool.h>

// Stub the functions that require kernel support
int hemem_wp_page(uint64_t addr) {
  (void)addr;
  return 0;
}

int hemem_migrate_up(uint64_t addr) {
  (void)addr;
  return 0;
}

int hemem_migrate_down(uint64_t addr) {
  (void)addr;
  return 0;
}

int hemem_get_bits(uint64_t addr) {
  (void)addr;
  return 0;
}

int hemem_clear_bits(uint64_t addr) {
  (void)addr;
  return 0;
}

int hemem_tlb_shootdown(void) {
  return 0;
}
