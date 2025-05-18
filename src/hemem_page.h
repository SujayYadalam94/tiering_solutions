#pragma once

#include "defs.h"
#include "groups.h"
#include <cmath>
#include <pthread.h>

enum pagetypes { HUGEP = 0, BASEP = 1, NPAGETYPES };
enum pbuftype { DRAMREAD = 0, NVMREAD = 1, WRITE = 2, NPBUFTYPES };
// defaults to Model since this would be all 0
enum prediction_type { MODEL = 0, ARMS = 1, NPREDICTIONTYPES };

struct hemem_page {
    uint64_t va;
    uint64_t devdax_offset;
    bool in_dram;
    enum pagetypes pt;
    volatile bool migrating;
    bool present;
    uint16_t accesses[NPBUFTYPES][2];
    pthread_mutex_t page_lock;

    float w[WINDOW_SIZE];

    float score;
    float prev_score;
    uint16_t hot_age;
    bool can_promote;

    struct hemem_page *next, *prev;
    struct fifo_list *list;

    float accuracy;
    float model_score;
    float arms_score;
    prediction_type model_selection;
    uint64_t _padding[6];
};
static_assert(sizeof(struct hemem_page) == 192);