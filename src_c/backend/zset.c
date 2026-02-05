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
    avl_free(z->tree);    // frees val nodes
    free(z);
}

int zset_add(ZSet *z, sds member, double score) {
    double old_score;
    // dict_get returns pointer to internal SDS of value, which is just raw bytes of double here?
    // Wait, dict stores values as SDS.
    // In zset_add, value is just bytes of double. 
    // So dict val is an SDS that holds sizeof(double) bytes.
    sds v = dict_get(z->dict, member);
    
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
            dict_put(z->dict, member, (sds)&score, 0); 
            // WAIT! (sds)&score casts double* to sds. This is dangerous! 
            // dict_put expects SDS. It calls sdslen.
            // We cannot just cast &score to sds.
            // We must create an SDS holding the double.
            
            sds score_sds = sdsnewlen(&score, sizeof(double));
            dict_put(z->dict, member, score_sds, 0); 
            sdsfree(score_sds);
        }
        is_new = 0;
    } else {
        // New
        avl_insert(z->tree, member, score);
        sds score_sds = sdsnewlen(&score, sizeof(double));
        dict_put(z->dict, member, score_sds, 0);
        sdsfree(score_sds);
    }
    return is_new;
}

int zset_rem(ZSet *z, sds member) {
    sds v = dict_get(z->dict, member);
    if (!v) return 0;
    
    double score;
    memcpy(&score, v, sizeof(double));
    
    avl_delete(z->tree, member, score);
    dict_delete(z->dict, member);
    return 1;
}

int zset_score(ZSet *z, sds member, double *score_out) {
    sds v = dict_get(z->dict, member);
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
    int len = (int)z->tree->count;
    if (start < 0) start = len + start;
    if (end < 0) end = len + end;
    
    if (start < 0) start = 0;
    if (start >= len) return;
    if (end < start) return;
    
    struct RangeCtx ctx;
    ctx.cur_idx = 0;
    ctx.start = start;
    ctx.end = end;
    ctx.user_cb = cb;
    ctx.user_arg = arg;
    
    avl_traverse(z->tree, range_cb, &ctx);
}

// Helper for rank calculation
struct RankCtx {
    sds target;
    int cur_idx;
    int found_rank;
};

static void rank_cb(AVLNode *node, void *arg) {
    struct RankCtx *ctx = (struct RankCtx*)arg;
    if (ctx->found_rank >= 0) return; // Already found
    if (sdscmp(node->key, ctx->target) == 0) {
        ctx->found_rank = ctx->cur_idx;
    }
    ctx->cur_idx++;
}

int zset_rank(ZSet *z, sds member) {
    // Check if member exists
    if (!dict_get(z->dict, member)) return -1;
    
    struct RankCtx ctx;
    ctx.target = member;
    ctx.cur_idx = 0;
    ctx.found_rank = -1;
    
    avl_traverse(z->tree, rank_cb, &ctx);
    return ctx.found_rank;
}
