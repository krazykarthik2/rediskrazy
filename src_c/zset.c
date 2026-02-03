#include "zset.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

ZSet *zset_create() {
    ZSet *z = (ZSet*)malloc(sizeof(ZSet));
    z->dict = (Dict*)malloc(sizeof(Dict));
    dict_init(z->dict, 16);
    z->tree = avl_create();
    return z;
}

void zset_free(ZSet *z) {
    if (!z) return;
    dict_destroy(z->dict); // frees members and scores
    free(z->dict);
    avl_free(z->tree);    // frees nodes (keys are duplicates in current avl impl)
    free(z);
}

int zset_add(ZSet *z, const char *member, double score) {
    double old_score;
    size_t val_len;
    char *v = dict_get(z->dict, member, strlen(member), &val_len);
    
    int is_new = 1;

    if (v) {
        // Exists
        memcpy(&old_score, v, sizeof(double));
        if (old_score != score) {
            // Update
            avl_delete(z->tree, member, old_score);
            avl_insert(z->tree, member, score);
            // Update dict
            // dict_put overwrites
            dict_put(z->dict, member, strlen(member), (char*)&score, sizeof(double), 0);
        }
        is_new = 0;
    } else {
        // New
        avl_insert(z->tree, member, score);
        dict_put(z->dict, member, strlen(member), (char*)&score, sizeof(double), 0);
    }
    return is_new;
}

int zset_rem(ZSet *z, const char *member) {
    size_t val_len;
    char *v = dict_get(z->dict, member, strlen(member), &val_len);
    if (!v) return 0;
    
    double score;
    memcpy(&score, v, sizeof(double));
    
    avl_delete(z->tree, member, score);
    dict_delete(z->dict, member, strlen(member));
    return 1;
}

int zset_score(ZSet *z, const char *member, double *score_out) {
    size_t val_len;
    char *v = dict_get(z->dict, member, strlen(member), &val_len);
    if (!v) return 0;
    
    if (score_out) memcpy(score_out, v, sizeof(double));
    return 1;
}

size_t zset_card(ZSet *z) {
    return z->tree->count;
}

// Helper structure for range callback
struct RangeCtx {
    int cur_idx;
    int start;
    int end;
    zset_callback user_cb;
    void *user_arg;
};

static void range_cb(AVLNode *node, void *arg) {
    struct RangeCtx *ctx = (struct RangeCtx*)arg;
    if (ctx->cur_idx >= ctx->start && ctx->cur_idx <= ctx->end) {
        ctx->user_cb(node->key, node->score, ctx->user_arg);
    }
    ctx->cur_idx++;
}

void zset_range(ZSet *z, int start, int end, zset_callback cb, void *arg) {
    // Handle negative indices like Redis
    int len = (int)z->tree->count;
    if (start < 0) start = len + start;
    if (end < 0) end = len + end;
    
    if (start < 0) start = 0;
    if (start >= len) return;
    if (end < start) return;
    
    // Efficiency: We should start traversal from 'start' if possible 
    // using avl_get_by_rank, then traverse next pointers if we had them.
    // But generic avl_traverse starts from 0.
    // avl_get_by_rank is O(start).
    // Generic traverse is O(N).
    // If we just use traverse with index check, it is O(N).
    // For MVP, O(N) is acceptable.
    // Optimization: Stop after 'end'. `traverse_in_order` doesn't support stopping early.
    // We can add a return value to callback to stop?
    // Let's implement full traversal for now.
    
    struct RangeCtx ctx;
    ctx.cur_idx = 0;
    ctx.start = start;
    ctx.end = end;
    ctx.user_cb = cb;
    ctx.user_arg = arg;
    
    avl_traverse(z->tree, range_cb, &ctx);
}
