#ifndef HEMEM_UTIL_H
#define HEMEM_UTIL_H

#include "hemem.h"

void page_migrate_up(struct hemem_page *page, uint64_t offset);
void page_migrate_down(struct hemem_page *page, uint64_t offset);

#endif
