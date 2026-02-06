#ifndef SQL_PARSER_H
#define SQL_PARSER_H

#include "../backend/sds.h"

#include "schema_manager.h"

typedef struct {
    int type; // 0:SELECT, 1:INSERT, 2:DELETE, 3:UPDATE, 4:CREATE_DB, 5:CREATE_TABLE, 6:USE, 7:LIST, 8:CLEAR, 9:DROP, 10:HELP
    sds key;
    sds val;
    sds table;
    int num_cols;
    Column *cols;
    int num_vals;
    sds *vals;
} SQLQuery;

typedef struct {
    char **headers;
    char ***rows;
    int num_cols;
    int num_rows;
    char *error;
    char *redis_cmd; // The translated Redis command
} SQLResult;

SQLResult process_sql(const char *sql);
void free_sql_result(SQLResult result);

#endif
