#ifndef RDB_H
#define RDB_H

#include "dict.h"
#include "zset.h"

// Save the current dataset (strings and zsets) to a file.
// Returns 0 on success, -1 on error.
int rdb_save(const char *filename, Dict *strings, Dict *zsets);

// Load the dataset from a file into the given dictionaries.
// Returns 0 on success, -1 on error (or file not found).
int rdb_load(const char *filename, Dict *strings, Dict *zsets);

#endif
