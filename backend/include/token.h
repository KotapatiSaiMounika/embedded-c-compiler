#ifndef TOKEN_H
#define TOKEN_H

/*
 * token.h - token types, the Token struct and the dynamic TokenArray.
 *
 * Ownership: every Token owns its `lexeme` (heap allocated). A TokenArray owns
 * its items AND their lexemes; token_array_free() releases everything.
 */

#include <stddef.h>
#include <stdio.h>

#include "strbuf.h"

typedef enum {
    /* keywords */
    TOKEN_INT,
    TOKEN_IF,
    TOKEN_ELSE,
    TOKEN_WHILE,
    TOKEN_UNSUPPORTED_KEYWORD, /* reserved C word outside the subset: for, break, float, ... */

    /* identifiers and literals */
    TOKEN_IDENTIFIER,
    TOKEN_INTEGER_LITERAL,

    /* arithmetic */
    TOKEN_PLUS,
    TOKEN_MINUS,
    TOKEN_MULTIPLY,
    TOKEN_DIVIDE,

    /* assignment / equality */
    TOKEN_ASSIGN,
    TOKEN_EQUAL,
    TOKEN_NOT_EQUAL,

    /* relational */
    TOKEN_LESS,
    TOKEN_GREATER,
    TOKEN_LESS_EQUAL,
    TOKEN_GREATER_EQUAL,

    /* logical */
    TOKEN_AND,
    TOKEN_OR,
    TOKEN_NOT,

    /* delimiters */
    TOKEN_SEMICOLON,
    TOKEN_COMMA,
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_LBRACE,
    TOKEN_RBRACE,

    TOKEN_EOF,
    TOKEN_INVALID
} TokenType;

typedef struct {
    TokenType type;
    char *lexeme; /* owned, NUL-terminated; "" for EOF */
    int line;     /* 1-based */
    int column;   /* 1-based */
} Token;

typedef struct {
    Token *items;
    int count;
    int capacity;
} TokenArray;

/* Name used in the UI / JSON, e.g. "KEYWORD_INT", "IDENTIFIER", "LESS_EQUAL". */
const char *token_type_name(TokenType type);
/* "keyword", "identifier", "literal", "operator", "delimiter", "eof", "invalid" */
const char *token_category(TokenType type);

/* Creates a token with its own copy of the first `len` bytes of `lexeme`.
 * On allocation failure the returned token has lexeme == NULL. */
Token token_create(TokenType type, const char *lexeme, size_t len, int line, int column);
void token_free(Token *token); /* frees the lexeme, sets it to NULL */
void token_print(const Token *token, FILE *out);

void token_array_init(TokenArray *array);
/* Appends a token (copying the lexeme). Returns 1 on success, 0 on out-of-memory. */
int token_array_push(TokenArray *array, TokenType type, const char *lexeme, size_t len,
                     int line, int column);
void token_array_free(TokenArray *array); /* frees all lexemes and the item array */
void token_array_print(const TokenArray *array, FILE *out);

/* JSON array: [{"lexeme","type","category","line","column"}, ...] */
void token_array_write_json(const TokenArray *array, StrBuf *sb);
char *token_array_to_json(const TokenArray *array); /* caller frees; NULL on OOM */

#endif /* TOKEN_H */
