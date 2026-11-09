#include "lexer.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *src;
    size_t len;
    size_t pos;
    int line;
    int col;
    TokenArray *tokens;
    DiagnosticList *diags;
    int errors;
    int oom;
} Lexer;

/* ---------- character helpers (ASCII only, locale independent) ---------- */

static int is_digit(int c) { return c >= '0' && c <= '9'; }
static int is_alpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
static int is_ident_char(int c) { return is_alpha(c) || is_digit(c); }

static int cur(const Lexer *lx) {
    return lx->pos < lx->len ? (unsigned char)lx->src[lx->pos] : 0;
}

static int peek(const Lexer *lx, size_t offset) {
    return lx->pos + offset < lx->len ? (unsigned char)lx->src[lx->pos + offset] : 0;
}

static void advance(Lexer *lx) {
    if (lx->pos >= lx->len) return;
    unsigned char c = (unsigned char)lx->src[lx->pos++];
    if (c == '\n') {
        lx->line++;
        lx->col = 1;
    } else if ((c & 0xC0) != 0x80) { /* UTF-8 continuation bytes do not add a column */
        lx->col++;
    }
}

/* ---------- reporting / emitting ---------- */

static void lex_error(Lexer *lx, int line, int col, const char *fmt, ...)
    DIAG_PRINTF(4, 5);

static void lex_error(Lexer *lx, int line, int col, const char *fmt, ...) {
    char msg[DIAG_MESSAGE_SIZE];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    diag_list_add(lx->diags, DIAG_STAGE_LEXICAL, DIAG_ERROR, line, col, "%s", msg);
    lx->errors++;
}

static void emit_text(Lexer *lx, TokenType type, const char *text, size_t n, int line, int col) {
    if (!token_array_push(lx->tokens, type, text, n, line, col)) lx->oom = 1;
}

/* Emits the source text from `start` up to the current position. */
static void emit(Lexer *lx, TokenType type, size_t start, int line, int col) {
    emit_text(lx, type, lx->src + start, lx->pos - start, line, col);
}

/* ---------- keywords ---------- */

typedef struct {
    const char *text;
    TokenType type;
} Keyword;

static const Keyword KEYWORDS[] = {
    {"int", TOKEN_INT},
    {"if", TOKEN_IF},
    {"else", TOKEN_ELSE},
    {"while", TOKEN_WHILE},
    /* C keywords that are NOT part of the subset: reported by the parser */
    {"for", TOKEN_UNSUPPORTED_KEYWORD},
    {"do", TOKEN_UNSUPPORTED_KEYWORD},
    {"switch", TOKEN_UNSUPPORTED_KEYWORD},
    {"case", TOKEN_UNSUPPORTED_KEYWORD},
    {"default", TOKEN_UNSUPPORTED_KEYWORD},
    {"break", TOKEN_UNSUPPORTED_KEYWORD},
    {"continue", TOKEN_UNSUPPORTED_KEYWORD},
    {"return", TOKEN_UNSUPPORTED_KEYWORD},
    {"goto", TOKEN_UNSUPPORTED_KEYWORD},
    {"float", TOKEN_UNSUPPORTED_KEYWORD},
    {"double", TOKEN_UNSUPPORTED_KEYWORD},
    {"char", TOKEN_UNSUPPORTED_KEYWORD},
    {"short", TOKEN_UNSUPPORTED_KEYWORD},
    {"long", TOKEN_UNSUPPORTED_KEYWORD},
    {"unsigned", TOKEN_UNSUPPORTED_KEYWORD},
    {"signed", TOKEN_UNSUPPORTED_KEYWORD},
    {"void", TOKEN_UNSUPPORTED_KEYWORD},
    {"struct", TOKEN_UNSUPPORTED_KEYWORD},
    {"union", TOKEN_UNSUPPORTED_KEYWORD},
    {"enum", TOKEN_UNSUPPORTED_KEYWORD},
    {"typedef", TOKEN_UNSUPPORTED_KEYWORD},
    {"sizeof", TOKEN_UNSUPPORTED_KEYWORD},
    {"const", TOKEN_UNSUPPORTED_KEYWORD},
    {"static", TOKEN_UNSUPPORTED_KEYWORD},
    {"extern", TOKEN_UNSUPPORTED_KEYWORD},
    {"volatile", TOKEN_UNSUPPORTED_KEYWORD},
    {"register", TOKEN_UNSUPPORTED_KEYWORD},
    {"auto", TOKEN_UNSUPPORTED_KEYWORD},
};

static TokenType keyword_type(const char *s, size_t n) {
    for (size_t i = 0; i < sizeof KEYWORDS / sizeof KEYWORDS[0]; i++) {
        if (strlen(KEYWORDS[i].text) == n && memcmp(KEYWORDS[i].text, s, n) == 0) {
            return KEYWORDS[i].type;
        }
    }
    return TOKEN_IDENTIFIER;
}

/* ---------- whitespace and comments ---------- */

static void skip_trivia(Lexer *lx) {
    for (;;) {
        int c = cur(lx);
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v') {
            advance(lx);
        } else if (c == '/' && peek(lx, 1) == '/') {
            while (cur(lx) != 0 && cur(lx) != '\n') advance(lx);
        } else if (c == '/' && peek(lx, 1) == '*') {
            int sl = lx->line, sc = lx->col;
            int closed = 0;
            advance(lx);
            advance(lx);
            while (cur(lx) != 0) {
                if (cur(lx) == '*' && peek(lx, 1) == '/') {
                    advance(lx);
                    advance(lx);
                    closed = 1;
                    break;
                }
                advance(lx);
            }
            if (!closed) lex_error(lx, sl, sc, "Unterminated comment: missing closing '*/'");
        } else {
            break;
        }
    }
}

/* ---------- token scanners ---------- */

static void scan_identifier(Lexer *lx, size_t start, int line, int col) {
    while (is_ident_char(cur(lx))) advance(lx);
    emit(lx, keyword_type(lx->src + start, lx->pos - start), start, line, col);
}

static void scan_number(Lexer *lx, size_t start, int line, int col) {
    while (is_digit(cur(lx))) advance(lx);

    if (is_ident_char(cur(lx))) { /* 12abc, 0x1F, 3_000 ... */
        while (is_ident_char(cur(lx))) advance(lx);
        size_t n = lx->pos - start;
        lex_error(lx, line, col, "Invalid number '%.*s' (only decimal integer literals are supported)",
                  (int)(n > 60 ? 60 : n), lx->src + start);
        emit(lx, TOKEN_INVALID, start, line, col);
        return;
    }
    if (lx->pos - start > 1 && lx->src[start] == '0') {
        lex_error(lx, line, col, "Leading zeros are not allowed (octal literals are not supported)");
        emit(lx, TOKEN_INVALID, start, line, col);
        return;
    }
    emit(lx, TOKEN_INTEGER_LITERAL, start, line, col);
}

static void unsupported_operator(Lexer *lx, size_t start, int line, int col) {
    lex_error(lx, line, col, "Operator '%.*s' is not supported in this C subset",
              (int)(lx->pos - start), lx->src + start);
    emit(lx, TOKEN_INVALID, start, line, col);
}

static void invalid_char(Lexer *lx, size_t start, int line, int col) {
    int c = (unsigned char)lx->src[start];

    if (c >= 0x80) { /* consume the whole UTF-8 sequence as ONE invalid token */
        advance(lx);
        while ((cur(lx) & 0xC0) == 0x80) advance(lx);
        char hex[64];
        size_t used = 0;
        for (size_t i = start; i < lx->pos && used + 5 < sizeof hex; i++) {
            used += (size_t)snprintf(hex + used, sizeof hex - used, "\\x%02X",
                                     (unsigned)(unsigned char)lx->src[i]);
        }
        lex_error(lx, line, col,
                  "Invalid non-ASCII character %s (only plain ASCII source is supported)", hex);
        emit_text(lx, TOKEN_INVALID, hex, used, line, col);
        return;
    }

    advance(lx);
    if (c < 0x20 || c == 0x7F) {
        char hex[8];
        int n = snprintf(hex, sizeof hex, "\\x%02X", (unsigned)c);
        lex_error(lx, line, col, "Invalid control character %s", hex);
        emit_text(lx, TOKEN_INVALID, hex, (size_t)n, line, col);
        return;
    }

    const char *hint = "";
    switch (c) {
        case '&': hint = " (bitwise operators are not supported; did you mean '&&'?)"; break;
        case '|': hint = " (bitwise operators are not supported; did you mean '||'?)"; break;
        case '%': hint = " (the modulo operator is not supported)"; break;
        case '[':
        case ']': hint = " (arrays are not supported)"; break;
        default: break;
    }
    lex_error(lx, line, col, "Invalid character '%c'%s", c, hint);
    emit(lx, TOKEN_INVALID, start, line, col);
}

static void scan_token(Lexer *lx) {
    size_t start = lx->pos;
    int line = lx->line, col = lx->col;
    int c = cur(lx);

    if (is_alpha(c)) {
        scan_identifier(lx, start, line, col);
        return;
    }
    if (is_digit(c)) {
        scan_number(lx, start, line, col);
        return;
    }

    switch (c) {
        case '+':
            advance(lx);
            if (cur(lx) == '+' || cur(lx) == '=') {
                advance(lx);
                unsupported_operator(lx, start, line, col);
            } else {
                emit(lx, TOKEN_PLUS, start, line, col);
            }
            return;
        case '-':
            advance(lx);
            if (cur(lx) == '-' || cur(lx) == '=') {
                advance(lx);
                unsupported_operator(lx, start, line, col);
            } else {
                emit(lx, TOKEN_MINUS, start, line, col);
            }
            return;
        case '*':
            advance(lx);
            if (cur(lx) == '=') {
                advance(lx);
                unsupported_operator(lx, start, line, col);
            } else {
                emit(lx, TOKEN_MULTIPLY, start, line, col);
            }
            return;
        case '/': /* comments were already removed by skip_trivia() */
            advance(lx);
            if (cur(lx) == '=') {
                advance(lx);
                unsupported_operator(lx, start, line, col);
            } else {
                emit(lx, TOKEN_DIVIDE, start, line, col);
            }
            return;
        case '=':
            advance(lx);
            if (cur(lx) == '=') {
                advance(lx);
                emit(lx, TOKEN_EQUAL, start, line, col);
            } else {
                emit(lx, TOKEN_ASSIGN, start, line, col);
            }
            return;
        case '!':
            advance(lx);
            if (cur(lx) == '=') {
                advance(lx);
                emit(lx, TOKEN_NOT_EQUAL, start, line, col);
            } else {
                emit(lx, TOKEN_NOT, start, line, col);
            }
            return;
        case '<':
            advance(lx);
            if (cur(lx) == '=') {
                advance(lx);
                emit(lx, TOKEN_LESS_EQUAL, start, line, col);
            } else {
                emit(lx, TOKEN_LESS, start, line, col);
            }
            return;
        case '>':
            advance(lx);
            if (cur(lx) == '=') {
                advance(lx);
                emit(lx, TOKEN_GREATER_EQUAL, start, line, col);
            } else {
                emit(lx, TOKEN_GREATER, start, line, col);
            }
            return;
        case '&':
            if (peek(lx, 1) == '&') {
                advance(lx);
                advance(lx);
                emit(lx, TOKEN_AND, start, line, col);
            } else {
                invalid_char(lx, start, line, col);
            }
            return;
        case '|':
            if (peek(lx, 1) == '|') {
                advance(lx);
                advance(lx);
                emit(lx, TOKEN_OR, start, line, col);
            } else {
                invalid_char(lx, start, line, col);
            }
            return;
        case ';': advance(lx); emit(lx, TOKEN_SEMICOLON, start, line, col); return;
        case ',': advance(lx); emit(lx, TOKEN_COMMA, start, line, col); return;
        case '(': advance(lx); emit(lx, TOKEN_LPAREN, start, line, col); return;
        case ')': advance(lx); emit(lx, TOKEN_RPAREN, start, line, col); return;
        case '{': advance(lx); emit(lx, TOKEN_LBRACE, start, line, col); return;
        case '}': advance(lx); emit(lx, TOKEN_RBRACE, start, line, col); return;

        case '#': /* preprocessor line: report once, skip the rest of the line */
            while (cur(lx) != 0 && cur(lx) != '\n') advance(lx);
            lex_error(lx, line, col, "Preprocessor directives are not supported in this C subset");
            emit_text(lx, TOKEN_INVALID, "#", 1, line, col);
            return;
        case '"':
        case '\'': { /* string / character literal: report once, skip to closing quote */
            advance(lx);
            while (cur(lx) != 0 && cur(lx) != c && cur(lx) != '\n') advance(lx);
            if (cur(lx) == c) advance(lx);
            lex_error(lx, line, col, "String and character literals are not supported in this C subset");
            emit_text(lx, TOKEN_INVALID, lx->src + start, 1, line, col);
            return;
        }
        default:
            invalid_char(lx, start, line, col);
            return;
    }
}

/* ---------- public API ---------- */

int lexer_tokenize(const char *source, TokenArray *tokens, DiagnosticList *diags) {
    if (!tokens || !diags) return -1;
    token_array_init(tokens);
    if (!source) source = "";

    Lexer lx;
    lx.src = source;
    lx.len = strlen(source);
    lx.pos = 0;
    lx.line = 1;
    lx.col = 1;
    lx.tokens = tokens;
    lx.diags = diags;
    lx.errors = 0;
    lx.oom = 0;

    if (lx.len > LEXER_MAX_SOURCE_LENGTH) {
        lex_error(&lx, 1, 1, "Source code is too large (%zu bytes, limit is %d bytes)", lx.len,
                  LEXER_MAX_SOURCE_LENGTH);
        emit_text(&lx, TOKEN_EOF, "", 0, 1, 1);
        return lx.oom ? -1 : lx.errors;
    }

    while (!lx.oom) {
        skip_trivia(&lx);
        if (cur(&lx) == 0) break;
        scan_token(&lx);
    }
    if (!lx.oom) emit_text(&lx, TOKEN_EOF, "", 0, lx.line, lx.col);

    if (lx.oom) {
        diag_list_add(diags, DIAG_STAGE_LEXICAL, DIAG_ERROR, lx.line, lx.col, "Out of memory");
        return -1;
    }
    return lx.errors;
}
