#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "sds.h"

// ... Existing functions ...
// I will rewrite the whole file to ensure clean state or append?
// Write full file is safer to avoid context matching issues with "end of file".

// Initialize SDS
sds sdsnewlen(const void *init, size_t initlen) {
    struct sdshdr *sh;
    if (init) {
        sh = (struct sdshdr*)malloc(sizeof(struct sdshdr) + initlen + 1);
    } else {
        sh = (struct sdshdr*)calloc(1, sizeof(struct sdshdr) + initlen + 1);
    }
    
    if (!sh) return NULL;
    
    sh->len = initlen;
    sh->free = 0;
    if (initlen && init) {
        memcpy(sh->buf, init, initlen);
    }
    sh->buf[initlen] = '\0';
    
    return (char*)sh->buf;
}

sds sdsnew(const char *init) {
    size_t initlen = (init == NULL) ? 0 : strlen(init);
    return sdsnewlen(init, initlen);
}

sds sdsempty(void) {
    return sdsnewlen("", 0);
}

size_t sdslen(const sds s) {
    struct sdshdr *sh = (void*)(s - sizeof(struct sdshdr));
    return sh->len;
}

size_t sdsavail(const sds s) {
    struct sdshdr *sh = (void*)(s - sizeof(struct sdshdr));
    return sh->free;
}

void sdsfree(sds s) {
    if (s == NULL) return;
    free(s - sizeof(struct sdshdr));
}

static sds sdsMakeRoomFor(sds s, size_t addlen) {
    struct sdshdr *sh, *newsh;
    size_t free = sdsavail(s);
    size_t len, newlen;
    
    if (free >= addlen) return s;
    
    len = sdslen(s);
    sh = (void*)(s - sizeof(struct sdshdr));
    
    newlen = (len + addlen);
    if (newlen < 1024*1024) 
        newlen *= 2;
    else 
        newlen += 1024*1024;
        
    newsh = (struct sdshdr*)realloc(sh, sizeof(struct sdshdr) + newlen + 1);
    if (!newsh) return NULL;
    
    newsh->free = newlen - len;
    return newsh->buf;
}

sds sdscatlen(sds s, const void *t, size_t len) {
    struct sdshdr *sh;
    size_t curlen = sdslen(s);
    
    s = sdsMakeRoomFor(s, len);
    if (!s) return NULL;
    
    sh = (void*)(s - sizeof(struct sdshdr));
    memcpy(s + curlen, t, len);
    sh->len = curlen + len;
    sh->free = sh->free - len;
    s[curlen + len] = '\0';
    
    return s;
}

sds sdscat(sds s, const char *t) {
    return sdscatlen(s, t, strlen(t));
}

sds sdscpylen(sds s, const char *t, size_t len) {
    struct sdshdr *sh = (void*)(s - sizeof(struct sdshdr));
    size_t totlen = sh->free + sh->len;
    
    if (totlen < len) {
        s = sdsMakeRoomFor(s, len - sh->len);
        if (!s) return NULL;
        sh = (void*)(s - sizeof(struct sdshdr));
        totlen = sh->free + sh->len;
    }
    
    memcpy(s, t, len);
    s[len] = '\0';
    sh->len = len;
    sh->free = totlen - len;
    return s;
}

sds sdscpy(sds s, const char *t) {
    return sdscpylen(s, t, strlen(t));
}

sds sdsdup(const sds s) {
    return sdsnewlen(s, sdslen(s));
}

sds sdsfromlonglong(long long value) {
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "%lld", value);
    return sdsnewlen(buf, len);
}

int sdscmp(sds s1, sds s2) {
    size_t l1 = sdslen(s1);
    size_t l2 = sdslen(s2);
    size_t minlen = (l1 < l2) ? l1 : l2;
    int cmp = memcmp(s1, s2, minlen);
    if (cmp == 0) return (int)(l1 - l2);
    return cmp;
}

void sdsfreesplitres(sds *argv, int argc) {
    if (!argv) return;
    while(argc--) sdsfree(argv[argc]);
    free(argv);
}

// Simple split args that doesn't handle all edge cases but good enough for now
sds* sdssplitargs(const char *line, int *argc) {
    const char *p = line;
    char *slot;
    sds *vector = NULL;
    *argc = 0;

    while(1) {
        while(*p && isspace(*p)) p++;
        if (!*p) break;

        const char *start = p;
        if (*p == '"' || *p == '\'') {
            char quote = *p++;
            start = p;
            while(*p && *p != quote) p++;
            slot = malloc(p-start+1);
            memcpy(slot,start,p-start);
            slot[p-start] = '\0';
            if (*p) p++;
        } else {
            while(*p && !isspace(*p)) p++;
            slot = malloc(p-start+1);
            memcpy(slot,start,p-start);
            slot[p-start] = '\0';
        }
        
        vector = realloc(vector, ((*argc)+1)*sizeof(sds));
        vector[*argc] = sdsnew(slot);
        free(slot);
        (*argc)++;
    }
    return vector;
}
