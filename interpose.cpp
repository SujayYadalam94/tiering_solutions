#include "arms_kernel.h"
#include "defs.h"
#include <cstddef>
#include <dlfcn.h>
#include <string.h>
#include <unistd.h>

__thread int32_t malloc_call_depth = 0;
__thread int32_t malloc_sample_counter = 0;

// Weak glibc entry point used to avoid infinite recursion while resolving malloc via dlsym
extern "C" void *__libc_malloc(size_t) __attribute__((weak));

static bool malloc_hook_disabled = false;

// Detect Python (or user opt-out) and disable the malloc hook to avoid LD_PRELOAD crashes there.
__attribute__((constructor)) static void init_malloc_hook()
{
    const char *env_disable = getenv("ARMS_DISABLE_MALLOC_HOOK");
    if (env_disable && strcmp(env_disable, "1") == 0)
    {
        malloc_hook_disabled = true;
        return;
    }

    const char *env_force = getenv("ARMS_FORCE_MALLOC_HOOK");

    char exe[256];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n > 0)
    {
        exe[n] = '\0';
        const char *base = strrchr(exe, '/');
        base = base ? base + 1 : exe;
        if (!env_force && strncmp(base, "python", 6) == 0)
        {
            malloc_hook_disabled = true;
        }
    }
}

void *malloc(size_t size)
{
    if (malloc_hook_disabled)
    {
        if (__libc_malloc)
            return __libc_malloc(size);
        static void *(*real_malloc)(size_t) = nullptr;
        if (!real_malloc)
            real_malloc = reinterpret_cast<void *(*)(size_t)>(dlsym(RTLD_NEXT, "malloc"));
        return real_malloc ? real_malloc(size) : nullptr;
    }

    malloc_call_depth++;
    malloc_sample_counter++;

    static void *(*next)(size_t) = NULL;
    if (!next)
    {
        if (malloc_call_depth > 1 && __libc_malloc)
        {
            // Re-entrant during resolver path (common with Python); fall back to glibc malloc
            void *p = __libc_malloc(size);
            malloc_call_depth--;
            return p;
        }
        next = reinterpret_cast<void *(*)(size_t)>(dlsym(RTLD_NEXT, "malloc"));
        if (!next && __libc_malloc)
        {
            // If dlsym failed early, at least return glibc malloc to keep process alive
            void *p = __libc_malloc(size);
            malloc_call_depth--;
            return p;
        }
    }

    void *ptr = next ? next(size) : (__libc_malloc ? __libc_malloc(size) : nullptr);

    if (malloc_call_depth > 1 || !initialized || (malloc_sample_counter % MALLOC_SAMPLE_RATE) != 0)
    {
        malloc_call_depth--;
        return ptr;
    }

    pebs_log_malloc(ptr, size);

    malloc_call_depth--;

    return ptr;
}
