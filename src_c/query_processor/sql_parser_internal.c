#include "sql_parser_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void parser_init(Parser *p, const char *sql) {
    lexer_init(&p->lexer, sql);
    p->current = lexer_next(&p->lexer);
    p->error = NULL;
}

void parser_free(Parser *p) {
    token_free(p->current);
    if (p->error) free(p->error);
}

static void advance(Parser *p) {
    token_free(p->current);
    p->current = lexer_next(&p->lexer);
}

static int match(Parser *p, TokenType type) {
    if (p->current.type == type) {
        advance(p);
        return 1;
    }
    return 0;
}

static void set_error(Parser *p, const char *msg) {
    if (p->error) return;
    p->error = strdup(msg);
}

SQLQuery parse_query(Parser *p) {
    SQLQuery q = {0};
    q.type = -1;

    if (match(p, TOKEN_KEYWORD_SELECT)) {
        q.type = 0; // SELECT
        if (!match(p, TOKEN_ASTERISK)) {
            set_error(p, "Expected * after SELECT");
            return q;
        }
        if (!match(p, TOKEN_KEYWORD_FROM)) {
            set_error(p, "Expected FROM after SELECT *");
            return q;
        }
        if (p->current.type == TOKEN_IDENTIFIER) {
            q.table = sdsnew(p->current.text);
            advance(p);
        } else {
            set_error(p, "Expected table name");
            return q;
        }
        if (match(p, TOKEN_KEYWORD_WHERE)) {
            if (p->current.type == TOKEN_IDENTIFIER && strcasecmp(p->current.text, "key") == 0) {
                advance(p);
                if (match(p, TOKEN_EQUALS)) {
                    if (p->current.type == TOKEN_STRING || p->current.type == TOKEN_IDENTIFIER) {
                        q.key = sdsnew(p->current.text);
                        advance(p);
                    } else {
                        set_error(p, "Expected key value");
                    }
                } else {
                    set_error(p, "Expected = after key");
                }
            } else {
                set_error(p, "Expected 'key' in WHERE clause");
            }
        }
    } else if (match(p, TOKEN_KEYWORD_INSERT)) {
        q.type = 1; // INSERT
        if (!match(p, TOKEN_KEYWORD_INTO)) {
            set_error(p, "Expected INTO after INSERT");
            return q;
        }
        if (p->current.type == TOKEN_IDENTIFIER) {
            q.table = sdsnew(p->current.text);
            advance(p);
        }
        
        // Optional column list (key, val)
        if (match(p, TOKEN_LPAREN)) {
            // Skip columns for now, assume order is key, val
            while (p->current.type != TOKEN_RPAREN && p->current.type != TOKEN_EOF) {
                advance(p);
            }
            match(p, TOKEN_RPAREN);
        }

        if (!match(p, TOKEN_KEYWORD_VALUES)) {
            set_error(p, "Expected VALUES");
            return q;
        }

        if (match(p, TOKEN_LPAREN)) {
            q.vals = NULL;
            q.num_vals = 0;
            while (p->current.type == TOKEN_STRING || p->current.type == TOKEN_IDENTIFIER) {
                q.vals = realloc(q.vals, sizeof(sds) * (q.num_vals + 1));
                q.vals[q.num_vals] = sdsnew(p->current.text);
                
                if (q.num_vals == 0) q.key = sdsnew(p->current.text);
                else if (q.num_vals == 1) q.val = sdsnew(p->current.text);
                
                q.num_vals++;
                advance(p);
                if (!match(p, TOKEN_COMMA)) break;
            }
            match(p, TOKEN_RPAREN);
        }
    } else if (match(p, TOKEN_KEYWORD_UPDATE)) {
        q.type = 3; // UPDATE
        if (p->current.type == TOKEN_IDENTIFIER) {
            q.table = sdsnew(p->current.text);
            advance(p);
        }
        if (match(p, TOKEN_KEYWORD_SET)) {
            if (p->current.type == TOKEN_IDENTIFIER && strcasecmp(p->current.text, "val") == 0) {
                advance(p);
                if (match(p, TOKEN_EQUALS)) {
                    if (p->current.type == TOKEN_STRING || p->current.type == TOKEN_IDENTIFIER) {
                        q.val = sdsnew(p->current.text);
                        advance(p);
                    }
                }
            }
        }
        if (match(p, TOKEN_KEYWORD_WHERE)) {
             if (p->current.type == TOKEN_IDENTIFIER && strcasecmp(p->current.text, "key") == 0) {
                advance(p);
                if (match(p, TOKEN_EQUALS)) {
                    if (p->current.type == TOKEN_STRING || p->current.type == TOKEN_IDENTIFIER) {
                        q.key = sdsnew(p->current.text);
                        advance(p);
                    }
                }
             }
        }
    } else if (match(p, TOKEN_KEYWORD_DELETE)) {
        q.type = 2; // DELETE
        if (match(p, TOKEN_KEYWORD_FROM)) {
            if (p->current.type == TOKEN_IDENTIFIER) {
                q.table = sdsnew(p->current.text);
                advance(p);
            }
        }
        if (match(p, TOKEN_KEYWORD_WHERE)) {
             if (p->current.type == TOKEN_IDENTIFIER && strcasecmp(p->current.text, "key") == 0) {
                advance(p);
                if (match(p, TOKEN_EQUALS)) {
                    if (p->current.type == TOKEN_STRING || p->current.type == TOKEN_IDENTIFIER) {
                        q.key = sdsnew(p->current.text);
                        advance(p);
                    }
                }
             }
        }
    } else if (match(p, TOKEN_KEYWORD_CREATE)) {
        if (match(p, TOKEN_KEYWORD_DATABASE)) {
            q.type = 4; // CREATE_DB
            if (p->current.type == TOKEN_IDENTIFIER) {
                q.key = sdsnew(p->current.text); // Use key as db name
                advance(p);
            } else {
                set_error(p, "Expected database name");
            }
        } else if (match(p, TOKEN_KEYWORD_TABLE)) {
            q.type = 5; // CREATE_TABLE
            if (p->current.type == TOKEN_IDENTIFIER) {
                q.table = sdsnew(p->current.text);
                advance(p);
            } else {
                set_error(p, "Expected table name");
            }
            if (match(p, TOKEN_LPAREN)) {
                q.cols = NULL;
                q.num_cols = 0;
                while (p->current.type == TOKEN_IDENTIFIER) {
                    q.cols = realloc(q.cols, sizeof(Column) * (q.num_cols + 1));
                    Column *c = &q.cols[q.num_cols];
                    c->name = strdup(p->current.text);
                    c->not_null = 0;
                    c->is_primary = 0;
                    advance(p);
                    
                    if (match(p, TOKEN_KEYWORD_PRIMARY)) {
                        match(p, TOKEN_KEYWORD_KEY);
                        c->is_primary = 1;
                    }

                    if (match(p, TOKEN_KEYWORD_INT)) {
                        c->type = TYPE_INT;
                    } else if (match(p, TOKEN_KEYWORD_VARCHAR)) {
                        c->type = TYPE_STRING;
                        if (match(p, TOKEN_LPAREN)) { // Skip (size)
                             advance(p);
                             match(p, TOKEN_RPAREN);
                        }
                    }
                    
                    if (match(p, TOKEN_KEYWORD_PRIMARY)) { // case where type is before primary key
                        match(p, TOKEN_KEYWORD_KEY);
                        c->is_primary = 1;
                    }

                    if (match(p, TOKEN_KEYWORD_NOT_NULL)) {
                        c->not_null = 1;
                    }
                    
                    q.num_cols++;
                    if (!match(p, TOKEN_COMMA)) break;
                }
                match(p, TOKEN_RPAREN);
            }
        } else {
            set_error(p, "Expected DATABASE or TABLE after CREATE");
        }
    } else if (match(p, TOKEN_KEYWORD_USE)) {
        q.type = 6; // USE
        if (p->current.type == TOKEN_IDENTIFIER) {
            q.key = sdsnew(p->current.text);
            advance(p);
        } else {
            set_error(p, "Expected database name after USE");
        }
    } else if (match(p, TOKEN_KEYWORD_LIST)) {
        q.type = 7; // LIST
        if (match(p, TOKEN_KEYWORD_DATABASES)) {
            q.key = sdsnew("DATABASES");
        } else if (match(p, TOKEN_KEYWORD_TABLES)) {
            q.key = sdsnew("TABLES");
        } else {
            set_error(p, "Expected DATABASES or TABLES after LIST");
        }
    } else if (match(p, TOKEN_KEYWORD_CLEAR) || match(p, TOKEN_KEYWORD_CLS)) {
        q.type = 8; // CLEAR
    } else {
        set_error(p, "Unknown or unsupported SQL command");
    }

    return q;
}
