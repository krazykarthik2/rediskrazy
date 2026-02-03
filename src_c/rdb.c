#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "rdb.h"

#define RDB_MAGIC "REDIS0001"
#define RDB_TYPE_STRING 0
#define RDB_TYPE_ZSET 1
#define RDB_OPCODE_EOF 255

// Helpers for binary writing
static void write_u32(FILE *fp, uint32_t v) {
    fwrite(&v, sizeof(v), 1, fp);
}

static void write_double(FILE *fp, double v) {
    fwrite(&v, sizeof(v), 1, fp);
}

static void write_string(FILE *fp, const char *s, size_t len) {
    write_u32(fp, (uint32_t)len);
    if (len > 0) fwrite(s, 1, len, fp);
}

static void read_u32(FILE *fp, uint32_t *v) {
    fread(v, sizeof(*v), 1, fp);
}

static void read_double(FILE *fp, double *v) {
    fread(v, sizeof(*v), 1, fp);
}

static char *read_string(FILE *fp, size_t *len_out) {
    uint32_t len;
    if (fread(&len, sizeof(len), 1, fp) != 1) return NULL;
    char *buf = (char*)malloc(len + 1);
    if (len > 0) {
        if (fread(buf, 1, len, fp) != len) {
            free(buf);
            return NULL;
        }
    }
    buf[len] = '\0';
    if (len_out) *len_out = (size_t)len;
    return buf;
}

// ZSet callback for saving
typedef struct {
    FILE *fp;
    char *key;
} SaveCtx;

static void zset_save_cb(const char *member, double score, void *arg) {
    SaveCtx *ctx = (SaveCtx *)arg;
    write_double(ctx->fp, score);
    write_string(ctx->fp, member, strlen(member));
}

int rdb_save(const char *filename, Dict *strings, Dict *zsets) {
    FILE *fp = fopen(filename, "wb");
    if (!fp) return -1;
    
    // Magic
    fwrite(RDB_MAGIC, 1, 9, fp);
    
    // 1. Strings
    if (strings && strings->tab) {
        for (size_t i = 0; i <= strings->mask; ++i) {
            DictEntry *e = strings->tab[i];
            while (e) {
                // Check expiry logic if we implemented strict save policy (skip expired)
                // For now, save everything or check expiry? 
                // Let's check expiry to clean up.
                if (e->expire == 0 || e->expire > time(NULL)) {
                    fputc(RDB_TYPE_STRING, fp);
                    write_string(fp, e->key, e->key_len);
                    write_string(fp, e->val, e->val_len);
                    // Saving expiry? For MVP, ignoring expiry or saving permanent. 
                    // Let's ignore saving TTL for now as per plan focus on data.
                }
                e = e->next;
            }
        }
    }
    
    // 2. ZSets
    if (zsets && zsets->tab) {
        for (size_t i = 0; i <= zsets->mask; ++i) {
            DictEntry *e = zsets->tab[i];
            while (e) {
                ZSet *z = *(ZSet**)e->val;
                fputc(RDB_TYPE_ZSET, fp);
                write_string(fp, e->key, e->key_len);
                write_u32(fp, (uint32_t)zset_card(z));
                
                SaveCtx ctx = { fp, e->key };
                // Iterate all nodes
                zset_range(z, 0, -1, zset_save_cb, &ctx);
                
                e = e->next;
            }
        }
    }
    
    fputc(RDB_OPCODE_EOF, fp);
    fclose(fp);
    return 0;
}

int rdb_load(const char *filename, Dict *strings, Dict *zsets) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) return -1;
    
    char magic[10];
    if (fread(magic, 1, 9, fp) != 9) { fclose(fp); return -1; }
    magic[9] = '\0';
    if (strcmp(magic, RDB_MAGIC) != 0) { fclose(fp); return -1; }
    
    while (1) {
        int type = fgetc(fp);
        if (type == EOF || type == RDB_OPCODE_EOF) break;
        
        if (type == RDB_TYPE_STRING) {
            size_t klen, vlen;
            char *key = read_string(fp, &klen);
            char *val = read_string(fp, &vlen);
            if (key && val) {
                dict_put(strings, key, klen, val, vlen, 0);
            }
            if (key) free(key);
            if (val) free(val);
        } else if (type == RDB_TYPE_ZSET) {
            size_t klen;
            char *key = read_string(fp, &klen);
            uint32_t count;
            read_u32(fp, &count);
            
            // Create ZSet
            ZSet *z = zset_create();
            dict_put(zsets, key, klen, (char*)&z, sizeof(ZSet*), 0);
            
            for (uint32_t i = 0; i < count; i++) {
                double score;
                read_double(fp, &score);
                size_t mlen;
                char *member = read_string(fp, &mlen);
                if (member) {
                    zset_add(z, member, score);
                    free(member);
                }
            }
            if (key) free(key);
        } else {
            // Unknown type
            break; 
        }
    }
    
    fclose(fp);
    return 0;
}
