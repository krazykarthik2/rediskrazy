
#ifndef RESP_H
#define RESP_H

#include <stddef.h>

// Parse RESP array.
// Returns > 0 (bytes consumed) if successful, 0 if incomplete, -1 on error.
// argv_out is allocated and must be freed by caller (deep free).
int resp_parse_array(const char *in, int in_len, char ***argv_out, int *argc_out);

// Free argv array
void resp_free_argv(char **argv, int argc);

// Formatters (return stack buffers or help write to socket? For now just helpers)
// (Skipping complex formatters for MVP, server.c handles writing)

#endif
