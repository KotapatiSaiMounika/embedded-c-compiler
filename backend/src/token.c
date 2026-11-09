#include "token.h"

#include <stdlib.h>
#include <string.h>

const char *token_type_name(TokenType type) {
    switch (type) {
        case TOKEN_INT:                 return "KEYWORD_INT";
        case TOKEN_IF:                  return "KEYWORD_IF";
        case TOKEN_ELSE:                return "KEYWORD_ELSE";
        case TOKEN_WHILE:               return "KEYWORD_WHILE";
        case TOKEN_UNSUPPORTED_KEYWORD: return "UNSUPPORTED_KEYWORD";
        case TOKEN_IDENTIFIER:          return "IDENTIFIER";
        case TOKEN_INTEGER_LITERAL:     return "INTEGER_LITERAL";
        case TOKEN_PLUS:                return "PLUS";
        case TOKEN_MINUS:               return "MINUS";
        case TOKEN_MULTIPLY:            return "MULTIPLY";
        case TOKEN_DIVIDE:              return "DIVIDE";
        case TOKEN_ASSIGN:              return "ASSIGN";
        case TOKEN_EQUAL:               return "EQUAL";
        case TOKEN_NOT_EQUAL:           return "NOT_EQUAL";
        case TOKEN_LESS:                return "LESS";
        case TOKEN_GREATER:             return "GREATER";
        case TOKEN_LESS_EQUAL:          return "LESS_EQUAL";
        case TOKEN_GREATER_EQUAL:       return "GREATER_EQUAL";
        case TOKEN_AND:                 return "AND";
        case TOKEN_OR:                  return "OR";
        case TOKEN_NOT:                 return "NOT";
        case TOKEN_SEMICOLON:           return "SEMICOLON";
        case TOKEN_COMMA:               return "COMMA";
        case TOKEN_LPAREN:              return "LPAREN";
        case TOKEN_RPAREN:              return "RPAREN";
        case TOKEN_LBRACE:              return "LBRACE";
        case TOKEN_RBRACE:              return "RBRACE";
        case TOKEN_EOF:                 return "EOF";
        case TOKEN_INVALID:             return "INVALID";
    }
    return "UNKNOWN";
}

const char *token_category(TokenType type) {
    switch (type) {
        case TOKEN_INT:
        case TOKEN_IF:
        case TOKEN_ELSE:
        case TOKEN_WHILE:
        case TOKEN_UNSUPPORTED_KEYWORD:
            return "keyword";
        case TOKEN_IDENTIFIER:
            return "identifier";
        case TOKEN_INTEGER_LITERAL:
            return "literal";
        case TOKEN_PLUS:
        case TOKEN_MINUS:
        case TOKEN_MULTIPLY:
        case TOKEN_DIVIDE:
        case TOKEN_ASSIGN:
        case TOKEN_EQUAL:
        case TOKEN_NOT_EQUAL:
        case TOKEN_LESS:
        case TOKEN_GREATER:
        case TOKEN_LESS_EQUAL:
        case TOKEN_GREATER_EQUAL:
        case TOKEN_AND:
        case TOKEN_OR:
        case TOKEN_NOT:
            return "operator";
        case TOKEN_SEMICOLON:
        case TOKEN_COMMA:
        case TOKEN_LPAREN:
        case TOKEN_RPAREN:
        case TOKEN_LBRACE:
        case TOKEN_RBRACE:
            return "delimiter";
        case TOKEN_EOF:
            return "eof";
        case TOKEN_INVALID:
            return "invalid";
    }
    return "unknown";
}

Token token_create(TokenType type, const char *lexeme, size_t len, int line, int column) {
    Token t;
    t.type = type;
    t.lexeme = str_dup_n(lexeme ? lexeme : "", lexeme ? len : 0);
    t.line = line;
    t.column = column;
    return t;
}

void token_free(Token *token) {
    if (!token) return;
    free(token->lexeme);
    token->lexeme = NULL;
}

void token_print(const Token *token, FILE *out) {
    fprintf(out, "%-4d %-4d %-20s '%s'\n", token->line, token->column,
            token_type_name(token->type), token->lexeme ? token->lexeme : "");
}

void token_array_init(TokenArray *array) {
    array->items = NULL;
    array->count = 0;
    array->capacity = 0;
}

int token_array_push(TokenArray *array, TokenType type, const char *lexeme, size_t len,
                     int line, int column) {
    if (array->count == array->capacity) {
        int ncap = array->capacity ? array->capacity * 2 : 32;
        Token *ni = realloc(array->items, (size_t)ncap * sizeof(Token));
        if (!ni) return 0;
        array->items = ni;
        array->capacity = ncap;
    }
    Token t = token_create(type, lexeme, len, line, column);
    if (!t.lexeme) return 0;
    array->items[array->count++] = t;
    return 1;
}

void token_array_free(TokenArray *array) {
    for (int i = 0; i < array->count; i++) token_free(&array->items[i]);
    free(array->items);
    token_array_init(array);
}

void token_array_print(const TokenArray *array, FILE *out) {
    fprintf(out, "%-4s %-4s %-20s %s\n", "Line", "Col", "Type", "Lexeme");
    for (int i = 0; i < array->count; i++) token_print(&array->items[i], out);
}

void token_array_write_json(const TokenArray *array, StrBuf *sb) {
    sb_append_char(sb, '[');
    for (int i = 0; array && i < array->count; i++) {
        const Token *t = &array->items[i];
        if (i > 0) sb_append_char(sb, ',');
        sb_append(sb, "{\"lexeme\":");
        sb_append_json_string(sb, t->lexeme);
        sb_append(sb, ",\"type\":");
        sb_append_json_string(sb, token_type_name(t->type));
        sb_append(sb, ",\"category\":");
        sb_append_json_string(sb, token_category(t->type));
        sb_appendf(sb, ",\"line\":%d,\"column\":%d}", t->line, t->column);
    }
    sb_append_char(sb, ']');
}

char *token_array_to_json(const TokenArray *array) {
    StrBuf sb;
    sb_init(&sb);
    token_array_write_json(array, &sb);
    return sb_detach(&sb);
}
