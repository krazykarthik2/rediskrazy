
#include "dict.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static unsigned int hash_fn(const char *s, size_t len) {
    unsigned long h = 5381;
    for (size_t i = 0; i < len; i++) {
        h = ((h << 5) + h) + (unsigned char)s[i];
    }
    return (unsigned int)h;
}

void dict_init(Dict *h, size_t n) {
    h->size = 0;
    // ensure n is power of 2
    size_t pow2 = 4;
    while (pow2 < n) pow2 *= 2;
    h->mask = pow2 - 1;
    h->tab = (DictEntry**)calloc(pow2, sizeof(DictEntry*));
}

static void dict_resize(Dict *h) {
    size_t new_n = (h->mask + 1) * 2;
    DictEntry **new_tab = (DictEntry**)calloc(new_n, sizeof(DictEntry*));
    size_t new_mask = new_n - 1;
    
    for (size_t i = 0; i <= h->mask; ++i) {
        DictEntry *e = h->tab[i];
        while (e) {
            DictEntry *next = e->next;
            unsigned int code = hash_fn(e->key, e->key_len);
            size_t idx = code & new_mask;
            e->next = new_tab[idx];
            new_tab[idx] = e;
            e = next;
        }
    }
    free(h->tab);
    h->tab = new_tab;
    h->mask = new_mask;
}

static DictEntry *dict_lookup_node(Dict *h, const char *key, size_t key_len) {
    if (!h->tab) return NULL;
    unsigned int code = hash_fn(key, key_len);
    size_t idx = code & h->mask;
    DictEntry *e = h->tab[idx];
    while (e) {
        if (e->key_len == key_len && memcmp(e->key, key, key_len) == 0) return e;
        e = e->next;
    }
    return NULL;
}

void dict_put(Dict *h, const char *key, size_t key_len, const char *val, size_t val_len, time_t ttl_seconds) {
    if (!h->tab) dict_init(h, 4);
    
    DictEntry *e = dict_lookup_node(h, key, key_len);
    if (e) {
        free(e->val);
        e->val = (char*)malloc(val_len + 1);
        memcpy(e->val, val, val_len);
        e->val[val_len] = '\0'; // null terminate for safety, though we trust len
        e->val_len = val_len;
        e->expire = ttl_seconds > 0 ? time(NULL) + ttl_seconds : 0;
    } else {
        if (h->size >= (h->mask + 1)) {
            dict_resize(h);
        }
        DictEntry *n = (DictEntry*)malloc(sizeof(DictEntry));
        n->key = (char*)malloc(key_len + 1);
        memcpy(n->key, key, key_len);
        n->key[key_len] = '\0';
        n->key_len = key_len;
        
        n->val = (char*)malloc(val_len + 1);
        memcpy(n->val, val, val_len);
        n->val[val_len] = '\0';
        n->val_len = val_len;
        
        n->expire = ttl_seconds > 0 ? time(NULL) + ttl_seconds : 0;
        
        unsigned int code = hash_fn(n->key, n->key_len);
        size_t idx = code & h->mask;
        n->next = h->tab[idx];
        h->tab[idx] = n;
        h->size++;
    }
}

char *dict_get(Dict *h, const char *key, size_t key_len, size_t *val_len_out) {
    DictEntry *e = dict_lookup_node(h, key, key_len);
    if (!e) return NULL;
    if (e->expire > 0 && time(NULL) > e->expire) {
        dict_delete(h, key, key_len);
        return NULL;
    }
    if (val_len_out) *val_len_out = e->val_len;
    return e->val;
}

time_t dict_get_expiry(Dict *h, const char *key, size_t key_len) {
    DictEntry *e = dict_lookup_node(h, key, key_len);
    if (!e) return 0;
    // Check if expired already? 
    // Usually TTL command checks validity. 
    // If expired, it's effectively gone.
    if (e->expire > 0 && time(NULL) > e->expire) {
        dict_delete(h, key, key_len);
        return 0;
    }
    return e->expire;
}

void dict_delete(Dict *h, const char *key, size_t key_len) {
    if (!h->tab) return;
    unsigned int code = hash_fn(key, key_len);
    size_t idx = code & h->mask;
    DictEntry *cur = h->tab[idx];
    DictEntry *prev = NULL;
    while (cur) {
        if (cur->key_len == key_len && memcmp(cur->key, key, key_len) == 0) {
            if (prev) prev->next = cur->next;
            else h->tab[idx] = cur->next;
            free(cur->key);
            free(cur->val);
            free(cur);
            h->size--;
            return;
        }
        prev = cur;
        cur = cur->next;
    }
}

void dict_clear(Dict *h) {
    if (!h->tab) return;
    for (size_t i = 0; i <= h->mask; ++i) {
        DictEntry *e = h->tab[i];
        while (e) {
            DictEntry *next = e->next;
            free(e->key);
            free(e->val);
            free(e);
            e = next;
        }
        h->tab[i] = NULL;
    }
    h->size = 0;
    // Optional: could free tab and resize down, but keeping current size is fine
}

int dict_active_expire(Dict *h, int n_buckets) {
    if (!h->tab) return 0;
    time_t now = time(NULL);
    int expired_count = 0;
    for (int i = 0; i < n_buckets; ++i) {
        size_t idx = rand() & h->mask;
        DictEntry *cur = h->tab[idx];
        DictEntry *prev = NULL;
        while (cur) {
            if (cur->expire > 0 && now > cur->expire) {
                DictEntry *tofree = cur;
                cur = cur->next;
                if (prev) prev->next = cur;
                else h->tab[idx] = cur;
                free(tofree->key);
                free(tofree->val);
                free(tofree);
                h->size--;
                expired_count++;
            } else {
                prev = cur;
                cur = cur->next;
            }
        }
    }
    return expired_count;
}

void dict_destroy(Dict *h) {
    if (!h->tab) return;
    dict_clear(h);
    free(h->tab);
    h->tab = NULL;
    h->size = 0;
    h->mask = 0;
}
