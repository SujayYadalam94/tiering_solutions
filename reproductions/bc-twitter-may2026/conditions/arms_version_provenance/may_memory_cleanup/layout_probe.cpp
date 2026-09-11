// Compile to an object only; never link or execute the benchmark/library.
// Symbol sizes expose this compiler's ABI layout for frozen May headers.
#include "page.h"
#include "groups.h"
#include "logging.h"

char layout_page_info[sizeof(page_info)];
char layout_page_ptr[sizeof(page_ptr)];
char layout_score_entry[sizeof(score_entry)];
char layout_page_group[sizeof(page_group)];
char layout_group_tracker[sizeof(group_tracker)];
char layout_data_row[sizeof(data_row)];
char layout_access_log[sizeof(struct access_log)];
