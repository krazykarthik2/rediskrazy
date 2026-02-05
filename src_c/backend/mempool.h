#ifndef MEMPOOL_H
#define MEMPOOL_H

#include <stddef.h>

/* 
 * Simple Memory Pool Allocator
 * 
 * Provides:
 * - Fast allocation for fixed-size objects
 * - Reduced fragmentation
 * - Optional defragmentation support
 */

typedef struct MemBlock {
    struct MemBlock *next;
} MemBlock;

typedef struct MemPool {
    size_t block_size;      // Size of each block
    size_t chunk_size;      // Number of blocks per chunk
    MemBlock *free_list;    // Free block list
    void **chunks;          // Array of allocated chunks
    size_t num_chunks;      // Number of chunks allocated
    size_t capacity;        // Capacity of chunks array
    size_t allocated;       // Total blocks allocated
    size_t in_use;          // Blocks currently in use
} MemPool;

// Initialize a memory pool for objects of given size
void mempool_init(MemPool *pool, size_t block_size, size_t initial_blocks);

// Allocate from pool
void *mempool_alloc(MemPool *pool);

// Return to pool
void mempool_free(MemPool *pool, void *ptr);

// Destroy pool and free all memory
void mempool_destroy(MemPool *pool);

// Get statistics
size_t mempool_in_use(MemPool *pool);
size_t mempool_allocated(MemPool *pool);

// Defragmentation (compacts free list, returns reclaimed memory to system if possible)
void mempool_defrag(MemPool *pool);

#endif
