#include "sql_translator.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static SQLResult make_error(const char *msg) {
    SQLResult res = {0};
    res.error = strdup(msg);
    return res;
}

SQLResult translate_query(SQLQuery q, const char *parser_error) {
    if (parser_error) {
        return make_error(parser_error);
    }

    SQLResult res = {0};
    char buf[1024];

    switch (q.type) {
        case 0: // SELECT
            if (!q.key) return make_error("SELECT requires a WHERE key = '...' clause");
            snprintf(buf, sizeof(buf), "GET %s", q.key);
            res.redis_cmd = strdup(buf);
            
            res.num_cols = 2;
            res.headers = malloc(sizeof(char*) * 2);
            res.headers[0] = strdup("Key");
            res.headers[1] = strdup("Value");
            res.num_rows = 1;
            res.rows = malloc(sizeof(char**) * 1);
            res.rows[0] = malloc(sizeof(char*) * 2);
            res.rows[0][0] = strdup(q.key);
            res.rows[0][1] = strdup("PENDING"); // Backend will fill this
            break;

        case 1: // INSERT
            if (!q.key || !q.val) return make_error("INSERT requires key and value");
            snprintf(buf, sizeof(buf), "SET %s %s", q.key, q.val);
            res.redis_cmd = strdup(buf);
            
            res.num_cols = 1;
            res.headers = malloc(sizeof(char*) * 1);
            res.headers[0] = strdup("Status");
            res.num_rows = 1;
            res.rows = malloc(sizeof(char**) * 1);
            res.rows[0] = malloc(sizeof(char*) * 1);
            res.rows[0][0] = strdup("OK");
            break;

        case 2: // DELETE
            if (!q.key) return make_error("DELETE requires a WHERE key = '...' clause");
            snprintf(buf, sizeof(buf), "DEL %s", q.key);
            res.redis_cmd = strdup(buf);
            
            res.num_cols = 1;
            res.headers = malloc(sizeof(char*) * 1);
            res.headers[0] = strdup("Status");
            res.num_rows = 1;
            res.rows = malloc(sizeof(char**) * 1);
            res.rows[0] = malloc(sizeof(char*) * 1);
            res.rows[0][0] = strdup("OK");
            break;

        case 3: // UPDATE
            if (!q.key || !q.val) return make_error("UPDATE requires SET val = '...' and WHERE key = '...'");
            snprintf(buf, sizeof(buf), "SET %s %s", q.key, q.val);
            res.redis_cmd = strdup(buf);
            
            res.num_cols = 1;
            res.headers = malloc(sizeof(char*) * 1);
            res.headers[0] = strdup("Status");
            res.num_rows = 1;
            res.rows = malloc(sizeof(char**) * 1);
            res.rows[0] = malloc(sizeof(char*) * 1);
            res.rows[0][0] = strdup("OK");
            break;

        case 4: // CREATE_DB
            if (!q.key) return make_error("CREATE DATABASE requires a name");
            if (create_database(q.key) != 0) return make_error("Database already exists");
            res.redis_cmd = strdup("PING"); // No direct Redis equivalent, use PING as placeholder
            res.num_cols = 1;
            res.headers = malloc(sizeof(char*) * 1);
            res.headers[0] = strdup("Status");
            res.num_rows = 1;
            res.rows = malloc(sizeof(char**) * 1);
            res.rows[0] = malloc(sizeof(char*) * 1);
            res.rows[0][0] = strdup("Database Created");
            break;

        case 5: // CREATE_TABLE
            if (!q.table) return make_error("CREATE TABLE requires a name");
            // Hardcode to "default" DB for now if none specified
            if (create_table("default", q.table, q.cols, q.num_cols) != 0) {
                 // Try creating default db if it doesn't exist
                 create_database("default");
                 if (create_table("default", q.table, q.cols, q.num_cols) != 0)
                    return make_error("Failed to create table (maybe it exists or DB missing)");
            }
            res.redis_cmd = strdup("PING");
            res.num_cols = 1;
            res.headers = malloc(sizeof(char*) * 1);
            res.headers[0] = strdup("Status");
            res.num_rows = 1;
            res.rows = malloc(sizeof(char**) * 1);
            res.rows[0] = malloc(sizeof(char*) * 1);
            res.rows[0][0] = strdup("Table Created");
            break;

        default:
            return make_error("Unsupported query type");
    }

    return res;
}
