#ifndef ARMS_PAGING_H
#define ARMS_PAGING_H

#include <pthread.h>
#include <stdint.h>
#include <inttypes.h>

#include "../arms.h"


#define ADDRESS_MASK  ((uint64_t)0x00000ffffffff000UL)
#define FLAGS_MASK  ((uint64_t)0x0000000000000fffUL)

#define ARMS_PRESENT_FLAG  ((uint64_t)0x0000000000000001UL)
#define ARMS_WRITE_FLAG  ((uint64_t)0x0000000000000002UL)
#define ARMS_USER_FLAG   ((uint64_t)0x0000000000000004UL)
#define ARMS_PWT_FLAG    ((uint64_t)0x0000000000000008UL)
#define ARMS_PCD_FLAG    ((uint64_t)0x0000000000000010UL)
#define ARMS_ACCESSED_FLAG ((uint64_t)0x0000000000000020UL)
#define ARMS_DIRTY_FLAG  ((uint64_t)0x0000000000000040UL)
#define ARMS_HUGEPAGE_FLAG ((uint64_t)0x0000000000000080UL)


#define ARMS_PAGE_WALK_FLAGS (ARMS_PRESENT_FLAG |   \
               ARMS_WRITE_FLAG | \
         ARMS_USER_FLAG |  \
         ARMS_ACCESSED_FLAG |  \
         ARMS_DIRTY_FLAG)

#define ARMS_PWTPCD_FLAGS  (ARMS_PWT_FLAG | ARMS_PCD_FLAG)

#define ARMS_PGDIR_SHIFT 39
#define ARMS_PTRS_PER_PGD  512
#define ARMS_PUD_SHIFT   30
#define ARMS_PTRS_PER_PUD  512
#define ARMS_PMD_SHIFT   21
#define ARMS_PTRS_PER_PMD  512
#define ARMS_PAGE_SHIFT  12
#define ARMS_PTRS_PER_PTE  512

//#define EXAMINE_PGTABLES


void scan_pagetable();
void _scan_pagetable(bool clear_flag, uint64_t flag);

//void clear_accessed_bit(uint64_t pa);
//uint64_t get_accessed_bit(uint64_t pa);
//void clear_dirty_bit(uint64_t pa);
//uint64_t get_dirty_bit(uint64_t pa);
//
//uint64_t* va_to_pa(uint64_t va);

#ifdef EXAMINE_PGTABLES

struct pagemapEntry {
  uint64_t pfn : 54;
  unsigned int soft_dirty : 1;
  unsigned int exclusive : 1;
  unsigned int file_page : 1;
  unsigned int swapped : 1;
  unsigned int present : 1;
};

void *examine_pagetables();

#endif /*EXAMINE_PGTABLES*/

#endif /* ARMS_PAGING_H */

