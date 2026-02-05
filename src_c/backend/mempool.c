#include "mempool.h"
#include <stdlib.h>
#include <string.h>

static void mempool_grow(MemPool *pool) {
    // Allocate a new chunk
    size_t chunk_bytes = pool->block_size * pool->chunk_size;
    void *chunk = malloc(chunk_bytes);
    if (!chunk) return;
    
    // Grow chunks array if needed
    if (pool->num_chunks >= pool->capacity) {
        size_t new_cap = pool->capacity == 0 ? 4 : pool->capacity * 2;
        void **new_chunks = realloc(pool->chunks, new_cap * sizeof(void*));
        if (!new_chunks) { free(chunk); return; }
        pool->chunks = new_chunks;
        pool->capacity = new_cap;
    }
    
    pool->chunks[pool->num_chunks++] = chunk;
    pool->allocated += pool->chunk_size;
    
    // Add all blocks to free list
    char *ptr = (char*)chunk;
    for (size_t i = 0; i < pool->chunk_size; i++) {
        MemBlock *block = (MemBlock*)ptr;
        block->next = pool->free_list;
        pool->free_list = block;
        ptr += pool->block_size;
    }
}

void mempool_init(MemPool *pool, size_t block_size, size_t initial_blocks) {
    // Ensure block_size is at least pointer-sized for free list
    if (block_size < sizeof(MemBlock)) {
        block_size = sizeof(MemBlock);
    }
    
    // Align to 8 bytes
    block_size = (block_size + 7) & ~7;
    
    pool->block_size = block_size;
    pool->chunk_size = initial_blocks > 0 ? initial_blocks : 64;
    pool->free_list = NULL;
    pool->chunks = NULL;
    pool->num_chunks = 0;
    pool->capacity = 0;
    pool->allocated = 0;
    pool->in_use = 0;
    
    // Pre-allocate initial chunk
    if (initial_blocks > 0) {
        mempool_grow(pool);
    }
}

void *mempool_alloc(MemPool *pool) {
    if (!pool->free_list) {
        mempool_grow(pool);
    }
    
    if (!pool->free_list) return NULL;
    
    MemBlock *block = pool->free_list;
    pool->free_list = block->next;
    pool->in_use++;
    
    return (void*)block;
}

void mempool_free(MemPool *pool, void *ptr) {
    if (!ptr) return;
    
    MemBlock *block = (MemBlock*)ptr;
    block->next = pool->free_list;
    pool->free_list = block;
    pool->in_use--;
}

void mempool_destroy(MemPool *pool) {
    // Free all chunks
    for (size_t i = 0; i < pool->num_chunks; i++) {
        free(pool->chunks[i]);
    }
    free(pool->chunks);
    
    pool->chunks = NULL;
    pool->num_chunks = 0;
    pool->capacity = 0;
    pool->free_list = NULL;
    pool->allocated = 0;
    pool->in_use = 0;
}

size_t mempool_in_use(MemPool *pool) {
    return pool->in_use;
}

size_t mempool_allocated(MemPool *pool) {
    return pool->allocated;
}

void mempool_defrag(MemPool *pool) {
    // Simple defrag: if we have more than 2 empty chunks worth of space,
    // we could try to release some. For simplicity, just count and report.
    // Full defragmentation would require moving live objects which is complex.
    
    // For now, this is a no-op placeholder that could be extended
    // to track chunk utilization and free completely empty chunks.
    (void)pool;
}
