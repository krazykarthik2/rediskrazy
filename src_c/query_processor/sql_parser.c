#include "sql_parser.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "../backend/sds.h"

// Mock backend interface - In a real scenario, this would send RESP to the server
// For now, let's assume it converts SQL to a string command that we can send
// or just handle basic logic.

static SQLResult make_error(const char *msg) {
    SQLResult res = {0};
    res.error = strdup(msg);
    return res;
}

SQLResult process_sql(const char *sql) {
    SQLResult res = {0};
    sds s = sdsnew(sql);
    int argc;
    sds *argv = sdssplitargs(s, &argc);
    sdsfree(s);

    if (argc == 0) {
        sdsfreesplitres(argv, argc);
        return make_error("Empty query");
    }

    // Very naive SQL-like parsing
    if (strcasecmp(argv[0], "SELECT") == 0) {
        // SELECT * FROM strings WHERE key = 'k'
        // SELECT value FROM strings WHERE key = 'k'
        if (argc >= 6 && strcasecmp(argv[3], "FROM") == 0 && strcasecmp(argv[5], "WHERE") == 0) {
            char *key = NULL;
            // Parse key = 'val'
            for(int i=6; i<argc; i++) {
                if(strstr(argv[i], "key=")) {
                   key = strchr(argv[i], '=') + 1;
                }
            }
            
            if(!key && argc > 7 && strcmp(argv[7], "=") == 0) {
               key = argv[8];
            } else if (!key) {
               key = argv[7]; // Assume SELECT * FROM strings WHERE key 'val'
            }

            res.num_cols = 2;
            res.headers = malloc(sizeof(char*) * 2);
            res.headers[0] = strdup("Key");
            res.headers[1] = strdup("Value");
            
            // Here we would call the backend. For this implementation, 
            // the CLI will handle the actual network call to Redis.
            // This parser just identifies the "intent".
            
            res.num_rows = 1;
            res.rows = malloc(sizeof(char**) * 1);
            res.rows[0] = malloc(sizeof(char*) * 2);
            res.rows[0][0] = strdup(key ? key : "unknown");
            res.rows[0][1] = strdup("PENDING_BACKEND_CALL"); 
        } else {
            res = make_error("Unsupported SELECT syntax. Use: SELECT * FROM strings WHERE key = 'keyname'");
        }
    } else if (strcasecmp(argv[0], "INSERT") == 0) {
        // INSERT INTO strings VALUES ('k', 'v')
        if (argc >= 6 && strcasecmp(argv[1], "INTO") == 0 && strcasecmp(argv[4], "VALUES") == 0) {
             res.num_cols = 1;
             res.headers = malloc(sizeof(char*) * 1);
             res.headers[0] = strdup("Status");
             res.num_rows = 1;
             res.rows = malloc(sizeof(char**) * 1);
             res.rows[0] = malloc(sizeof(char*) * 1);
             res.rows[0][0] = strdup("INSERT_COMMAND_SENT");
        } else {
             res = make_error("Unsupported INSERT syntax. Use: INSERT INTO strings VALUES ('key', 'val')");
        }
    } else {
        res = make_error("Unknown command. Supported: SELECT, INSERT");
    }

    sdsfreesplitres(argv, argc);
    return res;
}

void free_sql_result(SQLResult result) {
    if (result.error) free(result.error);
    for (int i = 0; i < result.num_cols; i++) free(result.headers[i]);
    if (result.headers) free(result.headers);
    for (int i = 0; i < result.num_rows; i++) {
        for (int j = 0; j < result.num_cols; j++) free(result.rows[i][j]);
        free(result.rows[i]);
    }
    if (result.rows) free(result.rows);
}
