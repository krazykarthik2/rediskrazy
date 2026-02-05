#include "expheap.h"
#include <stdlib.h>
#include <string.h>

static void swap(ExpEntry *a, ExpEntry *b) {
    ExpEntry tmp = *a;
    *a = *b;
    *b = tmp;
}

static void sift_up(ExpHeap *heap, size_t idx) {
    while (idx > 0) {
        size_t parent = (idx - 1) / 2;
        if (heap->entries[idx].expire < heap->entries[parent].expire) {
            swap(&heap->entries[idx], &heap->entries[parent]);
            idx = parent;
        } else {
            break;
        }
    }
}

static void sift_down(ExpHeap *heap, size_t idx) {
    while (1) {
        size_t smallest = idx;
        size_t left = 2 * idx + 1;
        size_t right = 2 * idx + 2;
        
        if (left < heap->size && heap->entries[left].expire < heap->entries[smallest].expire) {
            smallest = left;
        }
        if (right < heap->size && heap->entries[right].expire < heap->entries[smallest].expire) {
            smallest = right;
        }
        
        if (smallest != idx) {
            swap(&heap->entries[idx], &heap->entries[smallest]);
            idx = smallest;
        } else {
            break;
        }
    }
}

void expheap_init(ExpHeap *heap, size_t initial_cap) {
    heap->capacity = initial_cap > 0 ? initial_cap : 16;
    heap->entries = (ExpEntry*)malloc(heap->capacity * sizeof(ExpEntry));
    heap->size = 0;
}

void expheap_push(ExpHeap *heap, sds key, time_t expire) {
    // Grow if needed
    if (heap->size >= heap->capacity) {
        heap->capacity *= 2;
        heap->entries = realloc(heap->entries, heap->capacity * sizeof(ExpEntry));
    }
    
    heap->entries[heap->size].key = sdsdup(key);
    heap->entries[heap->size].expire = expire;
    sift_up(heap, heap->size);
    heap->size++;
}

ExpEntry *expheap_peek(ExpHeap *heap) {
    if (heap->size == 0) return NULL;
    return &heap->entries[0];
}

ExpEntry expheap_pop(ExpHeap *heap) {
    ExpEntry result = {NULL, 0};
    if (heap->size == 0) return result;
    
    result = heap->entries[0];
    heap->size--;
    
    if (heap->size > 0) {
        heap->entries[0] = heap->entries[heap->size];
        sift_down(heap, 0);
    }
    
    return result;
}

void expheap_remove(ExpHeap *heap, sds key) {
    // Find the key
    for (size_t i = 0; i < heap->size; i++) {
        if (sdscmp(heap->entries[i].key, key) == 0) {
            sdsfree(heap->entries[i].key);
            
            // Replace with last element
            heap->size--;
            if (i < heap->size) {
                heap->entries[i] = heap->entries[heap->size];
                // Could go up or down
                if (i > 0 && heap->entries[i].expire < heap->entries[(i-1)/2].expire) {
                    sift_up(heap, i);
                } else {
                    sift_down(heap, i);
                }
            }
            return;
        }
    }
}

int expheap_empty(ExpHeap *heap) {
    return heap->size == 0;
}

size_t expheap_size(ExpHeap *heap) {
    return heap->size;
}

void expheap_destroy(ExpHeap *heap) {
    for (size_t i = 0; i < heap->size; i++) {
        sdsfree(heap->entries[i].key);
    }
    free(heap->entries);
    heap->entries = NULL;
    heap->size = 0;
    heap->capacity = 0;
}
