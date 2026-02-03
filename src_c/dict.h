
#ifndef DICT_H
#define DICT_H

#include <stddef.h>
#include <time.h>

typedef struct DictEntry {
    char *key;
    size_t key_len;
    char *val;
    size_t val_len;
    time_t expire; // 0 == no expiry
    struct DictEntry *next;
} DictEntry;

typedef struct {
    DictEntry **tab;
    size_t mask;
    size_t size;
} Dict;

// Initialize dictionary
void dict_init(Dict *d, size_t n);

// Put key-value (takes ownership of val copy if needed, but here we duplicate)
void dict_put(Dict *d, const char *key, size_t key_len, const char *val, size_t val_len, time_t ttl_seconds);

// Get value (returns internal string, do not free)
char *dict_get(Dict *d, const char *key, size_t key_len, size_t *val_len_out);

// Delete entry
void dict_delete(Dict *d, const char *key, size_t key_len);

// Get absolute expiry time (0 if none or not found - distinction via existence check)
time_t dict_get_expiry(Dict *d, const char *key, size_t key_len);

// Clear all entries (for FLUSHDB)
void dict_clear(Dict *d);

// Active expiration cycle (sample n buckets)
int dict_active_expire(Dict *d, int n_buckets);

// Destructor (optional, for clean shutdown)
// Destructor (optional, for clean shutdown)
void dict_destroy(Dict *d);

// Update expiry for existing key
int dict_set_expiry(Dict *d, const char *key, size_t key_len, time_t ttl_seconds);

#endif
