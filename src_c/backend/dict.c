#include "dict.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

static unsigned int hash_fn(const void *key, size_t len) {
    const unsigned char *s = key;
    unsigned long h = 5381;
    for (size_t i = 0; i < len; i++) {
        h = ((h << 5) + h) + s[i];
    }
    return (unsigned int)h;
}

static void _dictReset(dictht *ht) {
    ht->table = NULL;
    ht->size = 0;
    ht->sizemask = 0;
    ht->used = 0;
}

void dict_init(Dict *d, size_t n) {
    (void)n;
    _dictReset(&d->ht[0]);
    _dictReset(&d->ht[1]);
    d->rehashidx = -1;
}

int dict_rehash(Dict *d, int n) {
    if (d->rehashidx == -1) return 0;
    
    while (n-- && d->ht[0].used != 0) {
        DictEntry *de, *nextde;
        
        while (d->ht[0].table[d->rehashidx] == NULL) {
            d->rehashidx++;
            if (d->rehashidx >= (long)d->ht[0].size) {
                d->ht[0].used = 0; 
                goto end_rehash;
            }
        }
        
        de = d->ht[0].table[d->rehashidx];
        while (de) {
            nextde = de->next;
            unsigned int h = hash_fn(de->key, sdslen(de->key));
            size_t idx = h & d->ht[1].sizemask;
            
            de->next = d->ht[1].table[idx];
            d->ht[1].table[idx] = de;
            d->ht[0].used--;
            d->ht[1].used++;
            de = nextde;
        }
        d->ht[0].table[d->rehashidx] = NULL;
        d->rehashidx++;
    }
    
end_rehash:
    if (d->ht[0].used == 0) {
        free(d->ht[0].table);
        d->ht[0] = d->ht[1];
        _dictReset(&d->ht[1]);
        d->rehashidx = -1;
        return 0;
    }
    return 1;
}

static void _dictRehashStep(Dict *d) {
    if (d->rehashidx != -1) dict_rehash(d, 1);
}

static void dict_expand(Dict *d, size_t size) {
    if (d->rehashidx != -1) return;
    
    size_t realsize = 4;
    while (realsize < size) realsize *= 2;
    
    dictht n; 
    n.size = realsize;
    n.sizemask = realsize - 1;
    n.table = (DictEntry**)calloc(realsize, sizeof(DictEntry*));
    n.used = 0;
    
    if (d->ht[0].table == NULL) {
        d->ht[0] = n;
        return;
    }
    
    d->ht[1] = n;
    d->rehashidx = 0;
}

static DictEntry *dict_lookup_node(Dict *d, sds key) {
    if (d->ht[0].used + d->ht[1].used == 0) return NULL;
    
    if (d->rehashidx != -1) _dictRehashStep(d);
    
    size_t len = sdslen(key);
    unsigned int h = hash_fn(key, len);
    for (int table = 0; table <= 1; table++) {
        size_t idx = h & d->ht[table].sizemask;
        DictEntry *he = d->ht[table].table[idx];
        while (he) {
            // Compare sds keys
            if (sdslen(he->key) == len && memcmp(he->key, key, len) == 0) 
                return he;
            he = he->next;
        }
        if (d->rehashidx == -1) break;
    }
    return NULL;
}

void dict_put(Dict *d, sds key, sds val, time_t ttl_seconds) {
    if (d->ht[0].table == NULL) dict_expand(d, 4);
    
    if (d->rehashidx == -1 && d->ht[0].used >= d->ht[0].size) {
        dict_expand(d, d->ht[0].used * 2);
    }
    
    if (d->rehashidx != -1) _dictRehashStep(d);
    
    DictEntry *e = dict_lookup_node(d, key);
    if (e) {
        sdsfree(e->val);
        e->val = sdsdup(val);
        e->expire = ttl_seconds > 0 ? time(NULL) + ttl_seconds : 0;
        return;
    }
    
    unsigned int h = hash_fn(key, sdslen(key));
    int table = (d->rehashidx == -1) ? 0 : 1;
    size_t idx = h & d->ht[table].sizemask;
    
    DictEntry *n = (DictEntry*)malloc(sizeof(DictEntry));
    n->key = sdsdup(key);
    n->val = sdsdup(val);
    n->expire = ttl_seconds > 0 ? time(NULL) + ttl_seconds : 0;
    
    n->next = d->ht[table].table[idx];
    d->ht[table].table[idx] = n;
    d->ht[table].used++;
}

sds dict_get(Dict *d, sds key) {
    DictEntry *e = dict_lookup_node(d, key);
    if (!e) return NULL;
    if (e->expire > 0 && time(NULL) > e->expire) {
        dict_delete(d, key);
        return NULL;
    }
    return e->val;
}

time_t dict_get_expiry(Dict *d, sds key) {
    DictEntry *e = dict_lookup_node(d, key);
    if (!e) return 0;
    if (e->expire > 0 && time(NULL) > e->expire) {
        dict_delete(d, key);
        return 0;
    }
    return e->expire;
}

int dict_set_expiry(Dict *d, sds key, time_t ttl_seconds) {
    DictEntry *e = dict_lookup_node(d, key);
    if (!e) return 0;
    e->expire = ttl_seconds > 0 ? time(NULL) + ttl_seconds : 0;
    return 1;
}

void dict_delete(Dict *d, sds key) {
    if (d->ht[0].used + d->ht[1].used == 0) return;
    if (d->rehashidx != -1) _dictRehashStep(d);
    
    size_t len = sdslen(key);
    unsigned int h = hash_fn(key, len);
    for (int table = 0; table <= 1; table++) {
        size_t idx = h & d->ht[table].sizemask;
        DictEntry *cur = d->ht[table].table[idx];
        DictEntry *prev = NULL;
        while (cur) {
            if (sdslen(cur->key) == len && memcmp(cur->key, key, len) == 0) {
                if (prev) prev->next = cur->next;
                else d->ht[table].table[idx] = cur->next;
                sdsfree(cur->key);
                sdsfree(cur->val);
                free(cur);
                d->ht[table].used--;
                return;
            }
            prev = cur;
            cur = cur->next;
        }
        if (d->rehashidx == -1) break;
    }
}

void dict_clear(Dict *d) {
    for (int table = 0; table <= 1; table++) {
        if (!d->ht[table].table) continue;
        for (size_t i = 0; i < d->ht[table].size && d->ht[table].used > 0; i++) {
            DictEntry *e = d->ht[table].table[i];
            while (e) {
                DictEntry *next = e->next;
                sdsfree(e->key); sdsfree(e->val); free(e);
                e = next;
                d->ht[table].used--;
            }
        }
        free(d->ht[table].table);
        _dictReset(&d->ht[table]);
    }
    d->rehashidx = -1;
}

int dict_active_expire(Dict *d, int n_buckets) {
    if (d->ht[0].table == NULL) return 0;
    if (d->rehashidx != -1) _dictRehashStep(d); 
    
    time_t now = time(NULL);
    int expired_count = 0;
    
    int table = 0;
    if (d->rehashidx != -1 && (rand() % 2)) table = 1;
    if (d->ht[table].table == NULL) table = 1 - table;
    if (d->ht[table].table == NULL) return 0;

    for (int i = 0; i < n_buckets; ++i) {
        size_t idx = rand() & d->ht[table].sizemask;
        DictEntry *cur = d->ht[table].table[idx];
        DictEntry *prev = NULL;
        while (cur) {
            if (cur->expire > 0 && now > cur->expire) {
                DictEntry *tofree = cur;
                cur = cur->next;
                if (prev) prev->next = cur;
                else d->ht[table].table[idx] = cur;
                sdsfree(tofree->key);
                sdsfree(tofree->val);
                free(tofree);
                d->ht[table].used--;
                expired_count++;
            } else {
                prev = cur;
                cur = cur->next;
            }
        }
    }
    return expired_count;
}

void dict_destroy(Dict *d) {
    dict_clear(d);
}
