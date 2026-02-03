#ifndef ZSET_H
#define ZSET_H

#include "dict.h"
#include "avl.h"

typedef struct ZSet {
    Dict *dict;      // Maps Key (Member) -> Score (stored as double bytes)
    AVLTree *tree;   // Orders (Score, Member)
} ZSet;

ZSet *zset_create();
void zset_free(ZSet *zset);

// Add or update member score. Returns 1 if new, 0 if updated.
int zset_add(ZSet *zset, const char *member, double score);

// Remove member. Returns 1 if removed, 0 if not found.
int zset_rem(ZSet *zset, const char *member);

// Get score. Returns 1 if found (and populates score_out), 0 if not found.
int zset_score(ZSet *zset, const char *member, double *score_out);

// Rank range query.
// Returns list of members. 
// For MVP, we can return an array of strings? 
// Or a callback based approach similar to AVL.
// Let's use callback for flexibility and less allocations.
typedef void (*zset_callback)(const char *member, double score, void *arg);

void zset_range(ZSet *zset, int start, int end, zset_callback cb, void *arg);

// Count
size_t zset_card(ZSet *zset);

#endif
