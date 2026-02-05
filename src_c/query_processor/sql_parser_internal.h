#ifndef SQL_PARSER_INTERNAL_H
#define SQL_PARSER_INTERNAL_H

#include "sql_parser.h"
#include "sql_lexer.h"

typedef struct {
    Lexer lexer;
    Token current;
    char *error;
} Parser;

void parser_init(Parser *p, const char *sql);
SQLQuery parse_query(Parser *p);
void parser_free(Parser *p);

#endif
