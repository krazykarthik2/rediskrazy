#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <winsock2.h>
#include "rdb.h"
#include "sds.h"

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

static void write_string(FILE *fp, sds s) {
    size_t len = sdslen(s);
    write_u32(fp, (uint32_t)len);
    if (len > 0) fwrite(s, 1, len, fp);
}

static void read_u32(FILE *fp, uint32_t *v) {
    fread(v, sizeof(*v), 1, fp);
}

static void read_double(FILE *fp, double *v) {
    fread(v, sizeof(*v), 1, fp);
}

static sds read_string(FILE *fp) {
    uint32_t len;
    if (fread(&len, sizeof(len), 1, fp) != 1) return NULL;
    
    // Safety check just in case
    // if len is huge, malloc might fail, but sdsnewlen handles usage
    
    char *buf = (char*)malloc(len);
    if (len > 0) {
        if (fread(buf, 1, len, fp) != len) {
            free(buf);
            return NULL;
        }
    }
    
    sds s = sdsnewlen(buf, len);
    free(buf);
    return s;
}

// ZSet callback for saving
typedef struct {
    FILE *fp;
} SaveCtx;

static void rdb_zset_cb(sds member, double score, void *arg) {
    SaveCtx *ctx = (SaveCtx *)arg;
    write_double(ctx->fp, score);
    write_string(ctx->fp, member);
}

int rdb_save(const char *filename, Dict *strings, Dict *zsets) {
    FILE *fp = fopen(filename, "wb");
    if (!fp) return -1;
    
    // Magic
    fwrite(RDB_MAGIC, 1, 9, fp);
    
    // Save Strings
    for (int table = 0; table <= 1; table++) {
        if (!strings->ht[table].table) continue;
        for (size_t i = 0; i < strings->ht[table].size; ++i) {
            DictEntry *e = strings->ht[table].table[i];
            while (e) {
                if (e->expire == 0 || e->expire > time(NULL)) {
                     fputc(RDB_TYPE_STRING, fp); 
                     write_string(fp, e->key);
                     write_string(fp, e->val);
                }
                e = e->next;
            }
        }
    }

    // Save ZSets
    for (int table = 0; table <= 1; table++) {
        if (!zsets->ht[table].table) continue;
        for (size_t i = 0; i < zsets->ht[table].size; ++i) {
            DictEntry *e = zsets->ht[table].table[i];
            while (e) {
                if (e->expire == 0 || e->expire > time(NULL)) {
                    ZSet *z = *(ZSet**)e->val;
                    size_t card = zset_card(z);
                    
                    fputc(RDB_TYPE_ZSET, fp); 
                    write_string(fp, e->key);
                    
                    uint32_t c = htonl((uint32_t)card);
                    fwrite(&c, 4, 1, fp);
                    
                    SaveCtx ctx = { fp };
                    zset_range(z, 0, -1, rdb_zset_cb, &ctx);
                }
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
            sds key = read_string(fp);
            sds val = read_string(fp);
            if (key && val) {
                dict_put(strings, key, val, 0);
            }
            if (key) sdsfree(key);
            if (val) sdsfree(val);
        } else if (type == RDB_TYPE_ZSET) {
            sds key = read_string(fp);
            uint32_t count;
            read_u32(fp, &count);
            count = ntohl(count); // Was written with htonl
            
            // Create ZSet
            ZSet *z = zset_create();
            sds zptr = sdsnewlen(&z, sizeof(ZSet*));
            dict_put(zsets, key, zptr, 0);
            sdsfree(zptr);
            
            for (uint32_t i = 0; i < count; i++) {
                double score;
                read_double(fp, &score);
                sds member = read_string(fp);
                if (member) {
                    zset_add(z, member, score);
                    sdsfree(member);
                }
            }
            if (key) sdsfree(key);
        } else {
            break; 
        }
    }
    
    fclose(fp);
    return 0;
}
