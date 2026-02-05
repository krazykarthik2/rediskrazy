#ifndef AOFBUF_H
#define AOFBUF_H

#include <stdio.h>
#include <time.h>

/*
 * AOF Write Buffer with configurable fsync policy
 * 
 * Provides:
 * - Write batching for improved performance
 * - Configurable fsync policies (always, everysec, no)
 */

typedef enum {
    AOF_FSYNC_ALWAYS,   // fsync after every write (safest, slowest)
    AOF_FSYNC_EVERYSEC, // fsync every second (good balance)
    AOF_FSYNC_NO        // let OS handle it (fastest, less safe)
} AofFsyncPolicy;

typedef struct {
    FILE *fp;
    char *buffer;
    size_t buf_size;
    size_t buf_used;
    AofFsyncPolicy policy;
    time_t last_fsync;
} AofBuffer;

// Initialize buffered AOF writer
void aofbuf_init(AofBuffer *ab, FILE *fp, size_t buf_size, AofFsyncPolicy policy);

// Write data to buffer (may trigger flush)
void aofbuf_write(AofBuffer *ab, const char *data, size_t len);

// Write formatted string
void aofbuf_printf(AofBuffer *ab, const char *fmt, ...);

// Flush buffer to file
void aofbuf_flush(AofBuffer *ab);

// Check and perform periodic fsync if needed (call from event loop)
void aofbuf_periodic_fsync(AofBuffer *ab);

// Destroy buffer (flushes first)
void aofbuf_destroy(AofBuffer *ab);

// Set fsync policy at runtime
void aofbuf_set_policy(AofBuffer *ab, AofFsyncPolicy policy);

#endif
