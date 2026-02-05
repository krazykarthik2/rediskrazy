#ifndef SQL_TRANSLATOR_H
#define SQL_TRANSLATOR_H

#include "sql_parser.h"

SQLResult translate_query(SQLQuery q, const char *parser_error);

#endif
