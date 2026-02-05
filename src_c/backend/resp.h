
#ifndef RESP_H
#define RESP_H

#include <stddef.h>
#include "sds.h"

// Parse RESP array.
// Returns > 0 (bytes consumed) if successful, 0 if incomplete, -1 on error.
// argv_out is allocated (array of sds) and must be freed by caller (deep free).
// Passing address of sds* (sds **argv_out)
int resp_parse_array(const char *in, int in_len, sds **argv_out, int *argc_out);

// Free argv array (sds array)
// argv is sds* (char**)
void resp_free_argv(sds *argv, int argc);

// Formatters (return stack buffers or help write to socket? For now just helpers)
// (Skipping complex formatters for MVP, server.c handles writing)

#endif
