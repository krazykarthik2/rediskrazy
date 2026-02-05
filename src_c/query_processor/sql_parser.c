#include "sql_parser.h"
#include "sql_parser_internal.h"
#include "sql_translator.h"
#include "schema_manager.h"
#include <stdlib.h>
#include <string.h>

SQLResult process_sql(const char *sql) {
    Parser p;
    parser_init(&p, sql);
    
    SQLQuery q = parse_query(&p);
    SQLResult res = translate_query(q, p.error);
    
    // Clean up SQLQuery
    if (q.table) sdsfree(q.table);
    if (q.key) sdsfree(q.key);
    if (q.val) sdsfree(q.val);
    if (q.cols) {
        for (int i = 0; i < q.num_cols; i++) {
            free(q.cols[i].name);
        }
        free(q.cols);
    }
    
    parser_free(&p);
    return res;
}

void free_sql_result(SQLResult result) {
    if (result.error) free(result.error);
    
    if (result.headers) {
        for (int i = 0; i < result.num_cols; i++) {
            if (result.headers[i]) free(result.headers[i]);
        }
        free(result.headers);
    }
    
    if (result.rows) {
        for (int i = 0; i < result.num_rows; i++) {
            if (result.rows[i]) {
                for (int j = 0; j < result.num_cols; j++) {
                    if (result.rows[i][j]) free(result.rows[i][j]);
                }
                free(result.rows[i]);
            }
        }
        free(result.rows);
    }
    
    if (result.redis_cmd) free(result.redis_cmd);
}
