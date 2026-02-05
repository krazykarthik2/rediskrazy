#ifndef DICT_H
#define DICT_H

#include <stddef.h>
#include <time.h>
#include "sds.h"

typedef struct DictEntry {
    sds key;
    sds val;
    time_t expire; // 0 == no expiry
    struct DictEntry *next;
} DictEntry;

typedef struct dictht {
    DictEntry **table;
    size_t size;
    size_t sizemask;
    size_t used;
} dictht;

typedef struct {
    dictht ht[2];
    long rehashidx; /* rehashing not in progress if rehashidx == -1 */
} Dict;

// Initialize dictionary
void dict_init(Dict *d, size_t n);

// Put key-value (duplicates sds key and val)
void dict_put(Dict *d, sds key, sds val, time_t ttl_seconds);

// Get value (returns sds internal pointer)
sds dict_get(Dict *d, sds key);

// Delete entry
void dict_delete(Dict *d, sds key);

// Get absolute expiry time
time_t dict_get_expiry(Dict *d, sds key);

// Clear all entries
void dict_clear(Dict *d);

// Active expiration cycle
int dict_active_expire(Dict *d, int n_buckets);

// Destructor
void dict_destroy(Dict *d);

// Update expiry
int dict_set_expiry(Dict *d, sds key, time_t ttl_seconds);

#endif
