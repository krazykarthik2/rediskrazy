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
    Database *db = get_active_database();
    const char *db_name = db ? db->name : "default";

    switch (q.type) {
        case 0: // SELECT
            if (!q.key) {
                if (!q.table) return make_error("SELECT requires a table or a WHERE key = '...' clause");
                snprintf(buf, sizeof(buf), "KEYS %s:%s:*", db_name, q.table);
                res.redis_cmd = strdup(buf);
                res.num_cols = 1;
                res.headers = malloc(sizeof(char*) * 1);
                res.headers[0] = strdup("Note");
                res.num_rows = 1;
                res.rows = malloc(sizeof(char**) * 1);
                res.rows[0] = malloc(sizeof(char*) * 1);
                res.rows[0][0] = strdup("Use 'select * from table where key=...' for now. Full scan PENDING.");
                return res; 
            }
            
            Table *t = db ? get_table(db, q.table) : NULL;
            if (t) {
                // Relational-style fetch (HGETALL)
                snprintf(buf, sizeof(buf), "HGETALL %s:%s:%s", db_name, q.table, q.key);
                res.redis_cmd = strdup(buf);
                res.num_cols = t->num_columns;
                res.headers = malloc(sizeof(char*) * t->num_columns);
                for(int i=0; i<t->num_columns; i++) res.headers[i] = strdup(t->columns[i].name);
                res.num_rows = 1;
                res.rows = malloc(sizeof(char**) * 1);
                res.rows[0] = malloc(sizeof(char*) * t->num_columns);
                for(int i=0; i<t->num_columns; i++) res.rows[0][i] = strdup("PENDING");
            } else {
                if (q.table) snprintf(buf, sizeof(buf), "GET %s:%s:%s", db_name, q.table, q.key);
                else snprintf(buf, sizeof(buf), "GET %s", q.key);
                res.redis_cmd = strdup(buf);
                res.num_cols = 2;
                res.headers = malloc(sizeof(char*) * 2);
                res.headers[0] = strdup("Key");
                res.headers[1] = strdup("Value");
                res.num_rows = 1;
                res.rows = malloc(sizeof(char**) * 1);
                res.rows[0] = malloc(sizeof(char*) * 2);
                res.rows[0][0] = strdup(q.key);
                res.rows[0][1] = strdup("PENDING");
            }
            break;

        case 1: // INSERT
            if (q.num_vals == 0) return make_error("INSERT requires values");
            t = db ? get_table(db, q.table) : NULL;
            if (t) {
                // Relational-style insert (HSET)
                int pk_idx = -1;
                for(int i=0; i<t->num_columns; i++) if(t->columns[i].is_primary) pk_idx = i;
                if(pk_idx == -1) pk_idx = 0; // Default to first col
                
                if(q.num_vals < pk_idx + 1) return make_error("Primary key value missing");
                const char *pk_val = q.vals[pk_idx];
                
                sds hset_cmd = sdsempty();
                hset_cmd = sdscat(hset_cmd, "HSET ");
                hset_cmd = sdscat(hset_cmd, db_name);
                hset_cmd = sdscat(hset_cmd, ":");
                hset_cmd = sdscat(hset_cmd, q.table);
                hset_cmd = sdscat(hset_cmd, ":");
                hset_cmd = sdscat(hset_cmd, pk_val);

                for(int i=0; i<t->num_columns && i < q.num_vals; i++) {
                    hset_cmd = sdscat(hset_cmd, " ");
                    hset_cmd = sdscat(hset_cmd, t->columns[i].name);
                    hset_cmd = sdscat(hset_cmd, " ");
                    hset_cmd = sdscat(hset_cmd, q.vals[i]);
                }
                res.redis_cmd = strdup(hset_cmd);
                sdsfree(hset_cmd);
            } else {
                if (!q.key || !q.val) return make_error("INSERT requires key and value");
                if (q.table) snprintf(buf, sizeof(buf), "SET %s:%s:%s %s", db_name, q.table, q.key, q.val);
                else snprintf(buf, sizeof(buf), "SET %s %s", q.key, q.val);
                res.redis_cmd = strdup(buf);
            }
            
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
            if (q.table) {
                snprintf(buf, sizeof(buf), "DEL %s:%s:%s", db_name, q.table, q.key);
            } else {
                snprintf(buf, sizeof(buf), "DEL %s", q.key);
            }
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
            if (q.table) {
                snprintf(buf, sizeof(buf), "SET %s:%s:%s %s", db_name, q.table, q.key, q.val);
            } else {
                snprintf(buf, sizeof(buf), "SET %s %s", q.key, q.val);
            }
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
            Database *active_db = get_active_database();
            const char *db_name = active_db ? active_db->name : "default";
            if (create_table(db_name, q.table, q.cols, q.num_cols) != 0) {
                 if (!active_db) {
                     create_database("default");
                     if (create_table("default", q.table, q.cols, q.num_cols) == 0) goto table_ok;
                 }
                 return make_error("Failed to create table (maybe it exists or DB missing)");
            }
        table_ok:
            res.redis_cmd = strdup("PING");
            res.num_cols = 1;
            res.headers = malloc(sizeof(char*) * 1);
            res.headers[0] = strdup("Status");
            res.num_rows = 1;
            res.rows = malloc(sizeof(char**) * 1);
            res.rows[0] = malloc(sizeof(char*) * 1);
            res.rows[0][0] = strdup("Table Created");
            break;

        case 6: // USE
            if (!q.key) return make_error("USE requires a database name");
            if (set_active_database(q.key) != 0) return make_error("Database does not exist");
            res.redis_cmd = strdup("PING");
            res.num_cols = 1;
            res.headers = malloc(sizeof(char*) * 1);
            res.headers[0] = strdup("Status");
            res.num_rows = 1;
            res.rows = malloc(sizeof(char**) * 1);
            res.rows[0] = malloc(sizeof(char*) * 1);
            res.rows[0][0] = strdup("Database changed");
            break;

        case 7: // LIST
            res.redis_cmd = strdup("PING");
            if (strcmp(q.key, "DATABASES") == 0) {
                int count;
                char **names = list_databases(&count);
                res.num_cols = 1;
                res.headers = malloc(sizeof(char*) * 1);
                res.headers[0] = strdup("Database");
                res.num_rows = count;
                res.rows = malloc(sizeof(char**) * count);
                for (int i = 0; i < count; i++) {
                    res.rows[i] = malloc(sizeof(char*) * 1);
                    res.rows[i][0] = names[i];
                }
                free(names);
            } else {
                Database *db = get_active_database();
                if (!db) return make_error("No active database. Use 'USE <db>'");
                int count;
                char **names = list_tables(db->name, &count);
                res.num_cols = 1;
                res.headers = malloc(sizeof(char*) * 1);
                res.headers[0] = strdup("Table");
                res.num_rows = count;
                res.rows = malloc(sizeof(char**) * count);
                for (int i = 0; i < count; i++) {
                    res.rows[i] = malloc(sizeof(char*) * 1);
                    res.rows[i][0] = names[i];
                }
                free(names);
            }
            break;

        case 8: // CLEAR
            res.redis_cmd = NULL; // No redis cmd
            res.num_cols = 1;
            res.headers = malloc(sizeof(char*) * 1);
            res.headers[0] = strdup("Action");
            res.num_rows = 1;
            res.rows = malloc(sizeof(char**) * 1);
            res.rows[0] = malloc(sizeof(char*) * 1);
            res.rows[0][0] = strdup("CLEAR_SCREEN");
            break;

        default:
            return make_error("Unsupported query type");
    }

    return res;
}
