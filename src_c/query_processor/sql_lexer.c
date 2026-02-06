#include "sql_lexer.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

void lexer_init(Lexer *l, const char *src) {
    l->src = src;
    l->pos = 0;
}

static Token make_token(TokenType type, const char *text, int len) {
    Token t;
    t.type = type;
    t.text = (char*)malloc(len + 1);
    memcpy(t.text, text, len);
    t.text[len] = '\0';
    t.length = len;
    return t;
}

static int is_ident(char c) {
    return isalnum(c) || c == '_' || c == '*';
}

Token lexer_next(Lexer *l) {
    while (l->src[l->pos] && isspace(l->src[l->pos])) {
        l->pos++;
    }

    if (!l->src[l->pos]) {
        return make_token(TOKEN_EOF, "", 0);
    }

    char c = l->src[l->pos];

    if (c == '=') {
        l->pos++;
        return make_token(TOKEN_EQUALS, "=", 1);
    }
    if (c == ',') {
        l->pos++;
        return make_token(TOKEN_COMMA, ",", 1);
    }
    if (c == '(') {
        l->pos++;
        return make_token(TOKEN_LPAREN, "(", 1);
    }
    if (c == ')') {
        l->pos++;
        return make_token(TOKEN_RPAREN, ")", 1);
    }
    if (c == '*') {
        l->pos++;
        return make_token(TOKEN_ASTERISK, "*", 1);
    }

    if (c == '\'' || c == '"') {
        char quote = c;
        int start = ++l->pos;
        while (l->src[l->pos] && l->src[l->pos] != quote) {
            l->pos++;
        }
        int len = l->pos - start;
        if (l->src[l->pos] == quote) l->pos++;
        return make_token(TOKEN_STRING, l->src + start, len);
    }

    if (is_ident(c)) {
        int start = l->pos;
        while (l->src[l->pos] && is_ident(l->src[l->pos])) {
            l->pos++;
        }
        int len = l->pos - start;
        char *text = (char*)malloc(len + 1);
        memcpy(text, l->src + start, len);
        text[len] = '\0';

        TokenType type = TOKEN_IDENTIFIER;
        if (strcasecmp(text, "SELECT") == 0) type = TOKEN_KEYWORD_SELECT;
        else if (strcasecmp(text, "INSERT") == 0) type = TOKEN_KEYWORD_INSERT;
        else if (strcasecmp(text, "UPDATE") == 0) type = TOKEN_KEYWORD_UPDATE;
        else if (strcasecmp(text, "DELETE") == 0) type = TOKEN_KEYWORD_DELETE;
        else if (strcasecmp(text, "FROM") == 0) type = TOKEN_KEYWORD_FROM;
        else if (strcasecmp(text, "WHERE") == 0) type = TOKEN_KEYWORD_WHERE;
        else if (strcasecmp(text, "INTO") == 0) type = TOKEN_KEYWORD_INTO;
        else if (strcasecmp(text, "VALUES") == 0) type = TOKEN_KEYWORD_VALUES;
        else if (strcasecmp(text, "SET") == 0) type = TOKEN_KEYWORD_SET;
        else if (strcasecmp(text, "CREATE") == 0) type = TOKEN_KEYWORD_CREATE;
        else if (strcasecmp(text, "DATABASE") == 0) type = TOKEN_KEYWORD_DATABASE;
        else if (strcasecmp(text, "TABLE") == 0) type = TOKEN_KEYWORD_TABLE;
        else if (strcasecmp(text, "USE") == 0) type = TOKEN_KEYWORD_USE;
        else if (strcasecmp(text, "LIST") == 0) type = TOKEN_KEYWORD_LIST;
        else if (strcasecmp(text, "DATABASES") == 0) type = TOKEN_KEYWORD_DATABASES;
        else if (strcasecmp(text, "TABLES") == 0) type = TOKEN_KEYWORD_TABLES;
        else if (strcasecmp(text, "PRIMARY") == 0) type = TOKEN_KEYWORD_PRIMARY;
        else if (strcasecmp(text, "KEY") == 0) type = TOKEN_KEYWORD_KEY;
        else if (strcasecmp(text, "VAL") == 0) type = TOKEN_KEYWORD_VAL;
        else if (strcasecmp(text, "CLEAR") == 0) type = TOKEN_KEYWORD_CLEAR;
        else if (strcasecmp(text, "CLS") == 0) type = TOKEN_KEYWORD_CLS;
        else if (strcasecmp(text, "HELP") == 0) type = TOKEN_KEYWORD_HELP;
        else if (strcasecmp(text, "DROP") == 0) type = TOKEN_KEYWORD_DROP;
        else if (strcasecmp(text, "INT") == 0) type = TOKEN_KEYWORD_INT;
        else if (strcasecmp(text, "VARCHAR") == 0) type = TOKEN_KEYWORD_VARCHAR;
        else if (strcasecmp(text, "STRING") == 0) type = TOKEN_KEYWORD_STRING;
        else if (strcasecmp(text, "NOT") == 0) {
            // Peek next for NULL
            int temp_pos = l->pos;
            while (l->src[temp_pos] && isspace(l->src[temp_pos])) temp_pos++;
            if (strncasecmp(l->src + temp_pos, "NULL", 4) == 0 && !is_ident(l->src[temp_pos + 4])) {
                l->pos = temp_pos + 4;
                type = TOKEN_KEYWORD_NOT_NULL;
            }
        }
        else if (strcasecmp(text, "NULL") == 0) type = TOKEN_KEYWORD_NULL;

        free(text);
        return make_token(type, l->src + start, len);
    }

    l->pos++;
    return make_token(TOKEN_ERROR, "Invalid character", 1);
}

void token_free(Token t) {
    if (t.text) free(t.text);
}
