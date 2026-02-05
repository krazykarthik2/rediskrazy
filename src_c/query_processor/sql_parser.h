#ifndef SQL_PARSER_H
#define SQL_PARSER_H

#include "../backend/sds.h"

typedef struct {
    int type; // 0: SELECT, 1: INSERT/SET, 2: DELETE, 3: UPDATE
    sds key;
    sds val;
    sds table;
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
