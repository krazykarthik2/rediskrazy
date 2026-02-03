
#include "resp.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

int resp_parse_array(const char *in, int in_len, char ***argv_out, int **arglens_out, int *argc_out) {
    int pos = 0;
    if (pos >= in_len) return 0; // Incomplete
    if (in[pos] != '*') return -1; // Not an array

    // read line (number of elements)
    char line[64];
    int i = pos + 1; int j = 0;
    while (i < in_len && j + 1 < (int)sizeof(line) && in[i] != '\r') line[j++] = in[i++];
    if (i + 1 >= in_len || in[i] != '\r' || in[i+1] != '\n') return 0; // Incomplete line
    line[j] = '\0';
    int n = atoi(line);
    pos = i + 2;
    
    if (n <= 0) { *argc_out = 0; *argv_out = NULL; if(arglens_out) *arglens_out = NULL; return pos; }
    
    char **argv = (char**)malloc(sizeof(char*) * n);
    int *arglens = (int*)malloc(sizeof(int) * n);
    
    for (int k = 0; k < n; ++k) {
        if (pos >= in_len) { free(argv); free(arglens); return 0; }
        if (in[pos] != '$') { free(argv); free(arglens); return -1; }
        
        // read length
        i = pos + 1; j = 0;
        while (i < in_len && j + 1 < (int)sizeof(line) && in[i] != '\r') line[j++] = in[i++];
        if (i + 1 >= in_len || in[i] != '\r' || in[i+1] != '\n') { free(argv); free(arglens); return 0; }
        line[j] = '\0';
        int len = atoi(line);
        pos = i + 2;
        
        if (len < 0) { free(argv); free(arglens); return -1; } // Unsupported null bulk string in array for now
        if (pos + len + 2 > in_len) { free(argv); free(arglens); return 0; } // Incomplete bulk string data
        
        char *s = (char*)malloc(len + 1);
        memcpy(s, in + pos, len);
        s[len] = '\0';
        argv[k] = s;
        arglens[k] = len;
        pos += len;
        
        if (pos + 1 >= in_len || in[pos] != '\r' || in[pos+1] != '\n') { 
            // Should be covered by check above but safety first
            for (int x=0;x<=k;++x) free(argv[x]); 
            free(argv); 
            free(arglens);
            return 0; 
        }
        pos += 2;
    }
    *argv_out = argv; 
    *argc_out = n; 
    if(arglens_out) *arglens_out = arglens; else free(arglens);
    return pos;
}

void resp_free_argv(char **argv, int argc) {
    if (!argv) return;
    for (int i = 0; i < argc; ++i) {
        if (argv[i]) free(argv[i]);
    }
    free(argv);
}
