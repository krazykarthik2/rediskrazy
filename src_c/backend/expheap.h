#ifndef EXPHEAP_H
#define EXPHEAP_H

#include <time.h>
#include "sds.h"

/*
 * Min-Heap for TTL Expiration Scheduling
 * 
 * Provides O(log n) insert and O(1) peek for nearest expiring key
 * Used for efficient active expiration instead of random sampling
 */

typedef struct {
    sds key;
    time_t expire;
} ExpEntry;

typedef struct {
    ExpEntry *entries;
    size_t size;
    size_t capacity;
} ExpHeap;

// Initialize heap
void expheap_init(ExpHeap *heap, size_t initial_cap);

// Add key with expiration time
void expheap_push(ExpHeap *heap, sds key, time_t expire);

// Peek at nearest expiring key (NULL if empty)
ExpEntry *expheap_peek(ExpHeap *heap);

// Remove and return nearest expiring key
ExpEntry expheap_pop(ExpHeap *heap);

// Remove a specific key (for key deletion/update)
void expheap_remove(ExpHeap *heap, sds key);

// Check if empty
int expheap_empty(ExpHeap *heap);

// Get size
size_t expheap_size(ExpHeap *heap);

// Free heap
void expheap_destroy(ExpHeap *heap);

#endif
