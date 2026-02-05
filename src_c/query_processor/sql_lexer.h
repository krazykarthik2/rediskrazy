#ifndef SQL_LEXER_H
#define SQL_LEXER_H

typedef enum {
    TOKEN_KEYWORD_SELECT,
    TOKEN_KEYWORD_INSERT,
    TOKEN_KEYWORD_UPDATE,
    TOKEN_KEYWORD_DELETE,
    TOKEN_KEYWORD_FROM,
    TOKEN_KEYWORD_WHERE,
    TOKEN_KEYWORD_INTO,
    TOKEN_KEYWORD_VALUES,
    TOKEN_KEYWORD_SET,
    TOKEN_IDENTIFIER,
    TOKEN_STRING,
    TOKEN_EQUALS,
    TOKEN_COMMA,
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_KEYWORD_CREATE,
    TOKEN_KEYWORD_DATABASE,
    TOKEN_KEYWORD_TABLE,
    TOKEN_KEYWORD_NOT_NULL,
    TOKEN_KEYWORD_NULL,
    TOKEN_KEYWORD_INT,
    TOKEN_KEYWORD_VARCHAR,
    TOKEN_KEYWORD_USE,
    TOKEN_KEYWORD_LIST,
    TOKEN_KEYWORD_DATABASES,
    TOKEN_KEYWORD_TABLES,
    TOKEN_ASTERISK,
    TOKEN_EOF,
    TOKEN_ERROR
} TokenType;

typedef struct {
    TokenType type;
    char *text;
    int length;
} Token;

typedef struct {
    const char *src;
    int pos;
} Lexer;

void lexer_init(Lexer *l, const char *src);
Token lexer_next(Lexer *l);
void token_free(Token t);

#endif
