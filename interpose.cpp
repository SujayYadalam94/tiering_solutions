#include "arms_kernel.h"
#include "defs.h"
#include <cstddef>
#include <dlfcn.h>

__thread int32_t malloc_call_depth = 0;
__thread int32_t malloc_sample_counter = 0;

void *malloc(size_t size)
{
    malloc_call_depth++;
    malloc_sample_counter++;

    static void *(*next)(size_t) = NULL;
    if (!next)
    {
        next = reinterpret_cast<void *(*)(size_t)>(dlsym(RTLD_NEXT, "malloc"));
    }

    void *ptr = next(size);

    if (malloc_call_depth > 1 || !initialized || (malloc_sample_counter % MALLOC_SAMPLE_RATE) != 0)
    {
        malloc_call_depth--;
        return ptr;
    }

    pebs_log_malloc(ptr, size);

    malloc_call_depth--;

    return ptr;
}
