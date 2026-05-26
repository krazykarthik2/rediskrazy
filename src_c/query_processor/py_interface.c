#include <stdlib.h>
#include <string.h>
#include "sql_parser.h"
#include "sql_translator.h"
#include "schema_manager.h"
#include "../backend/sds.h"

__declspec(dllexport) void py_schema_init(void) {
    schema_init();
}

__declspec(dllexport) const char* py_translate_sql(const char *sql, char **err_msg, int *num_cols, int *num_rows) {
    SQLResult res = process_sql(sql);
    if (res.error) {
        *err_msg = strdup(res.error);
        *num_cols = 0;
        *num_rows = 0;
        free_sql_result(res);
        return NULL;
    }
    *err_msg = NULL;
    *num_cols = res.num_cols;
    *num_rows = res.num_rows;
    const char *cmd = res.redis_cmd ? strdup(res.redis_cmd) : NULL;
    free_sql_result(res);
    return cmd;
}

__declspec(dllexport) char* py_get_headers(const char *sql, int *num_cols) {
    SQLResult res = process_sql(sql);
    if (res.error || !res.headers || res.num_cols <= 0) {
        *num_cols = 0;
        free_sql_result(res);
        return NULL;
    }
    *num_cols = res.num_cols;
    sds joined = sdsempty();
    for (int i = 0; i < res.num_cols; i++) {
        if (i > 0) joined = sdscat(joined, ",");
        joined = sdscat(joined, res.headers[i]);
    }
    char *ret = strdup(joined);
    sdsfree(joined);
    free_sql_result(res);
    return ret;
}

__declspec(dllexport) void py_free_string(char *ptr) {
    free(ptr);
}
