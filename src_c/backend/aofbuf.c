#include "aofbuf.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#ifdef _WIN32
#include <io.h>
#define fsync(fd) _commit(fd)
#else
#include <unistd.h>
#endif

void aofbuf_init(AofBuffer *ab, FILE *fp, size_t buf_size, AofFsyncPolicy policy) {
    ab->fp = fp;
    ab->buf_size = buf_size > 0 ? buf_size : 4096;
    ab->buffer = (char*)malloc(ab->buf_size);
    ab->buf_used = 0;
    ab->policy = policy;
    ab->last_fsync = time(NULL);
}

static void do_fsync(AofBuffer *ab) {
    fflush(ab->fp);
    int fd = fileno(ab->fp);
    if (fd >= 0) {
        fsync(fd);
    }
    ab->last_fsync = time(NULL);
}

void aofbuf_flush(AofBuffer *ab) {
    if (ab->buf_used > 0 && ab->fp) {
        fwrite(ab->buffer, 1, ab->buf_used, ab->fp);
        ab->buf_used = 0;
        
        if (ab->policy == AOF_FSYNC_ALWAYS) {
            do_fsync(ab);
        }
    }
}

void aofbuf_write(AofBuffer *ab, const char *data, size_t len) {
    if (!ab->fp) return;
    
    // If data is larger than buffer, flush and write directly
    if (len >= ab->buf_size) {
        aofbuf_flush(ab);
        fwrite(data, 1, len, ab->fp);
        if (ab->policy == AOF_FSYNC_ALWAYS) {
            do_fsync(ab);
        }
        return;
    }
    
    // If buffer would overflow, flush first
    if (ab->buf_used + len > ab->buf_size) {
        aofbuf_flush(ab);
    }
    
    // Add to buffer
    memcpy(ab->buffer + ab->buf_used, data, len);
    ab->buf_used += len;
}

void aofbuf_printf(AofBuffer *ab, const char *fmt, ...) {
    char tmp[4096];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(tmp, sizeof(tmp), fmt, args);
    va_end(args);
    
    if (len > 0) {
        aofbuf_write(ab, tmp, (size_t)len);
    }
}

void aofbuf_periodic_fsync(AofBuffer *ab) {
    if (ab->policy == AOF_FSYNC_EVERYSEC) {
        time_t now = time(NULL);
        if (now - ab->last_fsync >= 1) {
            aofbuf_flush(ab);
            do_fsync(ab);
        }
    }
}

void aofbuf_destroy(AofBuffer *ab) {
    aofbuf_flush(ab);
    free(ab->buffer);
    ab->buffer = NULL;
    ab->buf_size = 0;
    ab->buf_used = 0;
}

void aofbuf_set_policy(AofBuffer *ab, AofFsyncPolicy policy) {
    ab->policy = policy;
}
