#ifndef SDS_H
#define SDS_H

#include <sys/types.h>
#include <stdarg.h>

// Type alias: sds is just a char* pointer to the buffer
typedef char *sds;

// The struct is hidden behind the pointer
struct sdshdr {
    int len;
    int free;
    char buf[];
};

// Create a new SDS string
sds sdsnew(const char *init);
sds sdsnewlen(const void *init, size_t initlen);
sds sdsempty(void);

// Free
void sdsfree(sds s);

// Utilities
size_t sdslen(const sds s);
size_t sdsavail(const sds s);
sds sdsdup(const sds s);
sds sdsgrowzero(sds s, size_t len);
sds sdscatlen(sds s, const void *t, size_t len);
sds sdscat(sds s, const char *t);
sds sdscpylen(sds s, const char *t, size_t len);
sds sdscpy(sds s, const char *t);
sds sdsfromlonglong(long long value);
int sdscmp(sds s1, sds s2);
sds *sdssplitargs(const char *line, int *argc);
void sdsfreesplitres(sds *argv, int argc);
void sdssetlen(sds s, size_t newlen);

#endif
