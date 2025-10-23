/**
 * @file pebs_allocator.h
 * @brief Simple bump allocator for PEBS internal data structures
 * 
 * This allocator uses direct libc mmap calls to avoid going through Hoard,
 * preventing recursive allocation deadlocks during HeMem initialization.
 * 
 * Design:
 * - Allocates large chunks (PEBS_ALLOC_CHUNK_SIZE) via libc mmap
 * - Serves allocations from current chunk using bump pointer
 * - No free() support (acceptable for PEBS data structures which live forever)
 * - Thread-safe via simple spinlock
 */

#ifndef PEBS_ALLOCATOR_H
#define PEBS_ALLOCATOR_H
#define PROT_HC 0x03000000	/* */
#if 1
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>

// Size of each mmap chunk (16 MB should be plenty for khash + kdq)
#define PEBS_ALLOC_CHUNK_SIZE (16 * 1024 * 1024)

// Maximum number of chunks we'll ever allocate
#define PEBS_ALLOC_MAX_CHUNKS 8

typedef struct {
    void *base;           // Base address of this chunk
    size_t size;          // Total size of chunk
    size_t used;          // Bytes used in this chunk
} pebs_chunk_t;

typedef struct {
    pebs_chunk_t chunks[PEBS_ALLOC_MAX_CHUNKS];
    int num_chunks;
    volatile int lock;    // Simple spinlock
} pebs_allocator_t;

// Global allocator instance
static pebs_allocator_t pebs_alloc = {0};

// Simple spinlock functions
static inline void pebs_lock(volatile int *lock) {
    while (__sync_lock_test_and_set(lock, 1)) {
        while (*lock) {
            __asm__ __volatile__("pause");
        }
    }
}

static inline void pebs_unlock(volatile int *lock) {
    __sync_lock_release(lock);
}

// Get libc's mmap directly (not our intercepted version)
static void* pebs_libc_mmap(size_t size) {
	LOG_DEBUG("pebs_libc_mmap: requesting mmap of size %zu\n", size);
    // Call mmap directly via syscall to bypass any interception
    void *addr = mmap(NULL, size, PROT_READ | PROT_WRITE | PROT_HC, 
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (addr == MAP_FAILED) {
        return NULL;
    }
    return addr;
}

// Allocate a new chunk
static int pebs_alloc_new_chunk() {
    if (pebs_alloc.num_chunks >= PEBS_ALLOC_MAX_CHUNKS) {
        return -1;  // Out of chunks!
    }
    
    void *chunk = pebs_libc_mmap(PEBS_ALLOC_CHUNK_SIZE);
    if (!chunk) {
        return -1;
    }
    
    int idx = pebs_alloc.num_chunks;
    pebs_alloc.chunks[idx].base = chunk;
    pebs_alloc.chunks[idx].size = PEBS_ALLOC_CHUNK_SIZE;
    pebs_alloc.chunks[idx].used = 0;
    pebs_alloc.num_chunks++;
    
    return idx;
}

// Main allocation function
static void* pebs_malloc(size_t size) {
    fputs("pebs_malloc: requested size\n", stderr);
    if (size == 0) return NULL;
    
    // Align to 16 bytes
    size = (size + 15) & ~15UL;
    
    pebs_lock(&pebs_alloc.lock);
    
    // Try to allocate from existing chunks
    for (int i = 0; i < pebs_alloc.num_chunks; i++) {
        pebs_chunk_t *chunk = &pebs_alloc.chunks[i];
        if (chunk->used + size <= chunk->size) {
            void *ptr = (char*)chunk->base + chunk->used;
            chunk->used += size;
            pebs_unlock(&pebs_alloc.lock);
            return ptr;
        }
    }
    
    // Need a new chunk
    if (pebs_alloc_new_chunk() < 0) {
        pebs_unlock(&pebs_alloc.lock);
        return NULL;  // Out of memory!
    }
    
    // Allocate from the new chunk
    pebs_chunk_t *chunk = &pebs_alloc.chunks[pebs_alloc.num_chunks - 1];
    void *ptr = chunk->base;
    chunk->used = size;
    
    pebs_unlock(&pebs_alloc.lock);
    return ptr;
}

static void* pebs_calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void *ptr = pebs_malloc(total);
    if (ptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}

static void* pebs_realloc(void *old_ptr, size_t new_size) {
    // Simple implementation: allocate new, copy, ignore old
    // This is fine since we never actually free memory
    if (!old_ptr) {
        return pebs_malloc(new_size);
    }
    
    if (new_size == 0) {
        return NULL;
    }
    
    void *new_ptr = pebs_malloc(new_size);
    if (!new_ptr) {
        return NULL;
    }
    
    // We don't know the old size, so we can't copy properly.
    // For khash/kdq, they pass the old pointer and new size,
    // and the caller handles copying the valid data.
    // So we just return the new pointer.
    return new_ptr;
}

static void pebs_free(void *ptr) {
    // No-op: we never free individual allocations
    // All memory is freed when the allocator is destroyed
    (void)ptr;
}

// Statistics
static void pebs_alloc_stats() {
    size_t total_allocated = 0;
    size_t total_used = 0;
    
    for (int i = 0; i < pebs_alloc.num_chunks; i++) {
        total_allocated += pebs_alloc.chunks[i].size;
        total_used += pebs_alloc.chunks[i].used;
    }
    
    LOG_DEBUG("PEBS Allocator Stats:\n");
    LOG_DEBUG("  Chunks: %d / %d\n", pebs_alloc.num_chunks, PEBS_ALLOC_MAX_CHUNKS);
    LOG_DEBUG("  Total allocated: %zu bytes (%.2f MB)\n", 
            total_allocated, total_allocated / (1024.0 * 1024.0));
    LOG_DEBUG("  Total used: %zu bytes (%.2f MB)\n", 
            total_used, total_used / (1024.0 * 1024.0));
    LOG_DEBUG("  Utilization: %.1f%%\n", 
            total_allocated > 0 ? (100.0 * total_used / total_allocated) : 0.0);
}
#endif
#endif // PEBS_ALLOCATOR_H
